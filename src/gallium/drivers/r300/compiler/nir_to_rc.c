/*
 * Copyright © 2014-2015 Broadcom
 * SPDX-License-Identifier: MIT
 */

#include "nir_to_rc.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_deref.h"
#include "compiler/nir/nir_legacy.h"
#include "compiler/nir/nir_worklist.h"
#include "compiler/radeon_code.h"
#include "compiler/radeon_compiler.h"
#include "compiler/radeon_program.h"
#include "compiler/radeon_program_constants.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_ureg.h"
#include "tgsi/tgsi_util.h"
#include "util/u_debug.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "r300_nir.h"
#include "r300_screen.h"

struct ntr_insn {
   rc_opcode opcode;
   struct ureg_dst dst[2];
   struct ureg_src src[4];
   /* Texture sampler unit is RC metadata, not a SrcReg[] operand. */
   struct ureg_src tex_sampler;
   enum tgsi_texture_type tex_target;
   struct tgsi_texture_offset tex_offset[4];

   unsigned mem_qualifier;
   enum pipe_format mem_format;

   bool is_tex : 1;
   bool precise : 1;
};

struct ntr_block {
   /* Array of struct ntr_insn */
   struct util_dynarray insns;
   int start_ip;
   int end_ip;
};

struct ntr_immediate {
   float values[4];
};

struct ntr_reg_interval {
   uint32_t start, end;
};

struct ntr_compile {
   nir_shader *s;
   nir_function_impl *impl;
   struct pipe_screen *screen;
   struct ureg_program *ureg;
   struct r300_shader_semantics *semantics;

   /* Options */
   bool lower_fabs;

   bool addr_declared[3];
   struct ureg_dst addr_reg[3];

   /* if condition set up at the end of a block, for ntr_emit_if(). */
   struct ureg_src if_cond;

   /* TGSI temps for our NIR SSA and register values. */
   struct ureg_dst *reg_temp;
   struct ureg_src *ssa_temp;

   struct ntr_reg_interval *liveness;

   /* Map from nir_block to ntr_block */
   struct hash_table *blocks;
   struct ntr_block *cur_block;
   unsigned current_if_else;
   unsigned cf_label;

   /* Whether we're currently emitting instructiosn for a precise NIR instruction. */
   bool precise;

   unsigned num_temps;

   /* Map from NIR driver_location to RC input register. */
   struct ureg_src *input_index_map;
   uint64_t centroid_inputs;

   uint32_t first_ubo;

   /* RC-side state for direct emission. */
   struct radeon_compiler *compiler;
   /* one struct ntr_immediate per NIR load_const */
   struct util_dynarray immediates;
   /* UBO size in vec4s (0 if no UBO) */
   unsigned ubo_size;
   /* offset for IMMEDIATE indices in the compiler's constants table */
   unsigned immediate_offset;

   /* FS output tracking. */
   int fs_output_color_index[4];
   int fs_output_depth_index;
   int fs_output_index[FRAG_RESULT_MAX];
   unsigned num_outputs;
};

static struct ureg_dst
ntr_temp(struct ntr_compile *c)
{
   return ureg_dst_register(TGSI_FILE_TEMPORARY, c->num_temps++);
}

static struct ntr_block *
ntr_block_from_nir(struct ntr_compile *c, struct nir_block *block)
{
   struct hash_entry *entry = _mesa_hash_table_search(c->blocks, block);
   return entry->data;
}

static void ntr_emit_cf_list(struct ntr_compile *c, struct exec_list *list);
static void ntr_emit_cf_list_ureg(struct ntr_compile *c, struct exec_list *list);

static struct ntr_insn *
ntr_insn(struct ntr_compile *c, rc_opcode opcode, struct ureg_dst dst, struct ureg_src src0,
         struct ureg_src src1, struct ureg_src src2, struct ureg_src src3)
{
   struct ntr_insn insn = {
      .opcode = opcode,
      .dst = {dst, ureg_dst_undef()},
      .src = {src0, src1, src2, src3},
      .tex_sampler = ureg_src_undef(),
      .precise = c->precise,
   };
   util_dynarray_append(&c->cur_block->insns, insn);
   return util_dynarray_top_ptr(&c->cur_block->insns, struct ntr_insn);
}

#define NTR_OP00(name, op)                                                          \
   static inline void ntr_##name(struct ntr_compile *c)                             \
   {                                                                                \
      ntr_insn(c, op, ureg_dst_undef(), ureg_src_undef(), ureg_src_undef(),         \
               ureg_src_undef(), ureg_src_undef());                                 \
   }

#define NTR_OP01(name, op)                                                          \
   static inline void ntr_##name(struct ntr_compile *c, struct ureg_src src0)       \
   {                                                                                \
      ntr_insn(c, op, ureg_dst_undef(), src0, ureg_src_undef(), ureg_src_undef(),   \
               ureg_src_undef());                                                   \
   }

#define NTR_OP11(name, op)                                                          \
   static inline void ntr_##name(struct ntr_compile *c, struct ureg_dst dst,        \
                                 struct ureg_src src0)                              \
   {                                                                                \
      ntr_insn(c, op, dst, src0, ureg_src_undef(), ureg_src_undef(),                \
               ureg_src_undef());                                                   \
   }

#define NTR_OP12(name, op)                                                          \
   static inline void ntr_##name(struct ntr_compile *c, struct ureg_dst dst,        \
                                 struct ureg_src src0, struct ureg_src src1)        \
   {                                                                                \
      ntr_insn(c, op, dst, src0, src1, ureg_src_undef(), ureg_src_undef());         \
   }

#define NTR_OP13(name, op)                                                          \
   static inline void ntr_##name(struct ntr_compile *c, struct ureg_dst dst,        \
                                 struct ureg_src src0, struct ureg_src src1,        \
                                 struct ureg_src src2)                              \
   {                                                                                \
      ntr_insn(c, op, dst, src0, src1, src2, ureg_src_undef());                     \
   }

NTR_OP00(KILL, RC_OPCODE_KILP)
NTR_OP00(BGNLOOP, RC_OPCODE_BGNLOOP)
NTR_OP00(BRK, RC_OPCODE_BRK)
NTR_OP00(CONT, RC_OPCODE_CONT)
NTR_OP00(ELSE, RC_OPCODE_ELSE)
NTR_OP00(ENDIF, RC_OPCODE_ENDIF)
NTR_OP00(ENDLOOP, RC_OPCODE_ENDLOOP)

NTR_OP01(KILL_IF, RC_OPCODE_KIL)
NTR_OP01(IF, RC_OPCODE_IF)

NTR_OP11(MOV, RC_OPCODE_MOV)
NTR_OP11(ARL, RC_OPCODE_ARL)
NTR_OP11(DDX, RC_OPCODE_DDX)
NTR_OP11(DDY, RC_OPCODE_DDY)

NTR_OP12(ADD, RC_OPCODE_ADD)
NTR_OP12(MAX, RC_OPCODE_MAX)

NTR_OP13(CMP, RC_OPCODE_CMP)

/**
 * Interprets a nir_load_const used as a NIR src as a uint.
 *
 * For non-native-integers drivers, nir_load_const_instrs used by an integer ALU
 * instruction (or in a phi-web used by an integer ALU instruction) were
 * converted to floats and the ALU instruction swapped to the float equivalent.
 * However, this means that integer load_consts used by intrinsics (which don't
 * normally get that conversion) may have been reformatted to be floats.  Given
 * that all of our intrinsic nir_src_as_uint() calls are expected to be small,
 * we can just look and see if they look like floats and convert them back to
 * ints.
 */
static uint32_t
ntr_src_as_uint(struct ntr_compile *c, nir_src src)
{
   uint32_t val = nir_src_as_uint(src);
   if (val >= fui(1.0))
      val = (uint32_t)uif(val);
   return val;
}

/* Per-channel masks of def/use within the block, and the per-channel
 * livein/liveout for the block as a whole.
 */
struct ntr_live_reg_block_state {
   uint8_t *def, *use, *livein, *liveout, *defin, *defout;
};

struct ntr_live_reg_state {
   unsigned bitset_words;

   struct ntr_reg_interval *regs;

   /* Used in propagate_across_edge() */
   BITSET_WORD *tmp_live;

   struct ntr_live_reg_block_state *blocks;

   nir_block_worklist worklist;
};

static void
ntr_allocate_regs_unoptimized(struct ntr_compile *c, nir_function_impl *impl)
{
   for (int i = 0; i < c->num_temps; i++)
      ureg_DECL_temporary(c->ureg);
}

static void
ntr_read_input_output(struct ntr_compile *c, gl_varying_slot location, unsigned base)
{
   if (base >= c->semantics->num_total)
      c->semantics->num_total = base + 1;

   switch (location) {
   case VARYING_SLOT_POS:
      if (c->s->info.stage == MESA_SHADER_VERTEX)
         c->semantics->pos = base;
      else
         c->semantics->wpos = base;
      break;
   case VARYING_SLOT_PSIZ:
      c->semantics->psize = base;
      break;
   case VARYING_SLOT_COL0:
      c->semantics->color[0] = base;
      break;
   case VARYING_SLOT_COL1:
      c->semantics->color[1] = base;
      break;
   case VARYING_SLOT_BFC0:
      c->semantics->bcolor[0] = base;
      break;
   case VARYING_SLOT_BFC1:
      c->semantics->bcolor[1] = base;
      break;
   case VARYING_SLOT_FOGC:
      c->semantics->fog = base;
      break;
   case VARYING_SLOT_FACE:
      assert(c->s->info.stage == MESA_SHADER_FRAGMENT);
      c->semantics->face = base;
      break;
   case VARYING_SLOT_EDGE:
      assert(c->s->info.stage == MESA_SHADER_VERTEX);
      fprintf(stderr, "r300 VP: cannot handle edgeflag output.\n");
      break;
   default:
      if (location >= VARYING_SLOT_VAR0 && location <= VARYING_SLOT_VAR31) {
         unsigned index = location - VARYING_SLOT_VAR0;
         if (c->semantics->generic[index] == ATTR_UNUSED)
            c->semantics->num_generic++;
         c->semantics->generic[index] = base;
      } else {
         printf("Unhandled varying slot: %u\n", location);
         UNREACHABLE("Unhandled varying slot");
      }
      break;
   }
}

static unsigned
ntr_fs_output_index(struct ntr_compile *c, gl_frag_result location,
                    unsigned dual_source_blend_index)
{
   int color_index = mesa_frag_result_get_color_index(location);
   if (dual_source_blend_index) {
      assert(location == FRAG_RESULT_COLOR || location == FRAG_RESULT_DATA0);
      color_index += dual_source_blend_index;
   }

   int *output_index;
   if (color_index >= 0 && color_index < ARRAY_SIZE(c->fs_output_color_index)) {
      output_index = &c->fs_output_color_index[color_index];
   } else if (location == FRAG_RESULT_DEPTH) {
      output_index = &c->fs_output_depth_index;
   } else {
      assert(location < FRAG_RESULT_MAX);
      output_index = &c->fs_output_index[location];
   }

   if (*output_index < 0)
      *output_index = c->num_outputs++;

   return *output_index;
}

static struct ureg_dst
ntr_output_decl(struct ntr_compile *c, nir_intrinsic_instr *instr, uint32_t *frac)
{
   nir_io_semantics semantics = nir_intrinsic_io_semantics(instr);
   int base = nir_intrinsic_base(instr);
   *frac = nir_intrinsic_component(instr);

   struct ureg_dst out;
   if (c->s->info.stage == MESA_SHADER_FRAGMENT) {
      switch (semantics.location) {
      case FRAG_RESULT_DEPTH:
         *frac = 2; /* z write is the to the .z channel in TGSI */
         break;
      case FRAG_RESULT_STENCIL:
         *frac = 1;
         break;
      default:
         break;
      }

      out = ureg_dst_register(TGSI_FILE_OUTPUT,
                              ntr_fs_output_index(c, semantics.location,
                                                  semantics.dual_source_blend_index));
   } else {
      ntr_read_input_output(c, semantics.location, base);

      out = ureg_dst_register(TGSI_FILE_OUTPUT, base);
      c->num_outputs = MAX2(c->num_outputs, base + semantics.num_slots);
   }

   unsigned write_mask;
   if (nir_intrinsic_has_write_mask(instr))
      write_mask = nir_intrinsic_write_mask(instr);
   else
      write_mask = ((1 << instr->num_components) - 1) << *frac;

   write_mask = write_mask << *frac;
   return ureg_writemask(out, write_mask);
}

static bool
ntr_try_store_in_tgsi_output_with_use(struct ntr_compile *c, struct ureg_dst *dst, nir_src *src)
{
   *dst = ureg_dst_undef();

   if (nir_src_is_if(src))
      return false;

   if (nir_src_use_instr(src)->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(nir_src_use_instr(src));
   if (intr->intrinsic != nir_intrinsic_store_output || !nir_src_is_const(intr->src[1])) {
      return false;
   }

   uint32_t frac;
   *dst = ntr_output_decl(c, intr, &frac);
   dst->Index += ntr_src_as_uint(c, intr->src[1]);

   return frac == 0;
}

/* If this reg is used only for storing an output, then in the simple
 * cases we can write directly to the TGSI output instead of having
 * store_output emit its own MOV.
 */
static bool
ntr_try_store_reg_in_tgsi_output(struct ntr_compile *c, struct ureg_dst *dst,
                                 nir_intrinsic_instr *reg_decl)
{
   assert(reg_decl->intrinsic == nir_intrinsic_decl_reg);

   *dst = ureg_dst_undef();

   /* Look for a single use for try_store_in_tgsi_output */
   nir_src *use = NULL;
   nir_foreach_reg_load (src, reg_decl) {
      nir_intrinsic_instr *load = nir_instr_as_intrinsic(nir_src_use_instr(src));
      nir_foreach_use_including_if (load_use, &load->def) {
         /* We can only have one use */
         if (use != NULL)
            return false;

         use = load_use;
      }
   }

   if (use == NULL)
      return false;

   return ntr_try_store_in_tgsi_output_with_use(c, dst, use);
}

/* If this SSA def is used only for storing an output, then in the simple
 * cases we can write directly to the TGSI output instead of having
 * store_output emit its own MOV.
 */
static bool
ntr_try_store_ssa_in_tgsi_output(struct ntr_compile *c, struct ureg_dst *dst, nir_def *def)
{
   *dst = ureg_dst_undef();

   if (!list_is_singular(&def->uses))
      return false;

   nir_foreach_use_including_if (use, def) {
      return ntr_try_store_in_tgsi_output_with_use(c, dst, use);
   }
   UNREACHABLE("We have one use");
}

static void
ntr_setup_inputs(struct ntr_compile *c)
{
   if (c->s->info.stage != MESA_SHADER_FRAGMENT)
      return;

   unsigned num_inputs = 0;
   nir_foreach_shader_in_variable (var, c->s) {
      const struct glsl_type *type = var->type;
      unsigned array_len = glsl_count_attribute_slots(type, false);

      num_inputs = MAX2(num_inputs, var->data.driver_location + array_len);
   }

   c->input_index_map = ralloc_array(c, struct ureg_src, num_inputs);

   nir_foreach_shader_in_variable (var, c->s) {
      const struct glsl_type *type = var->type;
      unsigned array_len = glsl_count_attribute_slots(type, false);

      struct ureg_src decl;

      decl = ureg_src_register(TGSI_FILE_INPUT, var->data.driver_location);

      if (var->data.location == VARYING_SLOT_FACE) {
         struct ureg_dst temp = ntr_temp(c);
         /* tgsi docs say that floating point FACE will be positive for
          * frontface and negative for backface, but realistically
          * GLSL-to-TGSI had been doing MOV_SAT to turn it into 0.0 vs 1.0.
          * Copy that behavior, since some drivers (r300) have been doing a
          * 0.0 vs 1.0 backface (and I don't think anybody has a non-1.0
          * front face).
          */
         temp.Saturate = true;
         ntr_MOV(c, temp, decl);
         decl = ureg_src(temp);
      }

      for (unsigned i = 0; i < array_len; i++) {
         c->input_index_map[var->data.driver_location + i] = decl;
         c->input_index_map[var->data.driver_location + i].Index += i;
      }
   }
}

static int
ntr_sort_by_location(const nir_variable *a, const nir_variable *b)
{
   return a->data.location - b->data.location;
}

/**
 * Workaround for virglrenderer requiring that TGSI FS output color variables
 * are declared in order.  Besides, it's a lot nicer to read the TGSI this way.
 */
static void
ntr_setup_outputs(struct ntr_compile *c)
{
   if (c->s->info.stage != MESA_SHADER_FRAGMENT)
      return;

   nir_sort_variables_with_modes(c->s, ntr_sort_by_location, nir_var_shader_out);

   nir_foreach_shader_out_variable (var, c->s) {
      if (var->data.location == FRAG_RESULT_COLOR)
         ureg_property(c->ureg, TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS, 1);

      ntr_fs_output_index(c, var->data.location, var->data.index);
   }
}

static enum tgsi_texture_type
tgsi_texture_type_from_sampler_dim(enum glsl_sampler_dim dim, bool is_array)
{
   /* r300 has no array, multisample or buffer textures. */
   assert(!is_array);
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
      return TGSI_TEXTURE_1D;
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_EXTERNAL:
      return TGSI_TEXTURE_2D;
   case GLSL_SAMPLER_DIM_3D:
      return TGSI_TEXTURE_3D;
   case GLSL_SAMPLER_DIM_CUBE:
      return TGSI_TEXTURE_CUBE;
   case GLSL_SAMPLER_DIM_RECT:
      return TGSI_TEXTURE_RECT;
   default:
      UNREACHABLE("unknown sampler dim");
   }
}

static void
ntr_setup_uniforms(struct ntr_compile *c)
{
   /* lower_uniforms_to_ubo lowered non-sampler uniforms to UBOs. */
   unsigned size = 0;
   nir_foreach_variable_with_modes (var, c->s, nir_var_mem_ubo) {
      int ubo = var->data.driver_location;
      assert(ubo == 0 && size == 0);
      size = glsl_get_explicit_size(var->interface_type, false);
   }
   c->ubo_size = size ? DIV_ROUND_UP(size, 16) : 0;
}

static void
ntr_setup_registers(struct ntr_compile *c)
{
   assert(c->num_temps == 0);

   /* After that, allocate non-array regs in our virtual space that we'll
    * register-allocate before ureg emit.
    */
   nir_foreach_reg_decl_safe (nir_reg, nir_shader_get_entrypoint(c->s)) {
      assert(nir_intrinsic_num_array_elems(nir_reg) == 0);
      unsigned num_components = nir_intrinsic_num_components(nir_reg);
      unsigned index = nir_reg->def.index;

      struct ureg_dst decl;
      uint32_t write_mask = BITFIELD_MASK(num_components);

      if (!ntr_try_store_reg_in_tgsi_output(c, &decl, nir_reg)) {
         decl = ureg_writemask(ntr_temp(c), write_mask);
      }
      c->reg_temp[index] = decl;
   }
}

static struct ureg_src
ntr_get_load_const_src(struct ntr_compile *c, nir_load_const_instr *instr)
{
   int num_components = instr->def.num_components;

   /* Always emit a full vec4 immediate per load_const and let the
    * RC backend's constant packing handle it later.
    */
   float values[4] = {0};
   assert(instr->def.bit_size == 32);
   for (int i = 0; i < num_components; i++)
      values[i] = uif(instr->value[i].u32);

   unsigned index = util_dynarray_num_elements(&c->immediates, struct ntr_immediate);
   struct ntr_immediate *slot =
      util_dynarray_grow(&c->immediates, struct ntr_immediate, 1);
   memcpy(slot->values, values, sizeof(values));
   return ureg_src_register(TGSI_FILE_IMMEDIATE, index);
}

static struct ureg_src
ntr_reladdr(struct ntr_compile *c, struct ureg_src addr, int addr_index)
{
   assert(addr_index < ARRAY_SIZE(c->addr_reg));

   for (int i = 0; i <= addr_index; i++) {
      if (!c->addr_declared[i]) {
         c->addr_reg[i] = ureg_writemask(ureg_DECL_address(c->ureg), TGSI_WRITEMASK_X);
         c->addr_declared[i] = true;
      }
   }

   ntr_ARL(c, c->addr_reg[addr_index], addr);
   return ureg_scalar(ureg_src(c->addr_reg[addr_index]), 0);
}

/* Forward declare for recursion with indirects */
static struct ureg_src ntr_get_src(struct ntr_compile *c, nir_src src);

static struct ureg_src
ntr_get_chased_src(struct ntr_compile *c, nir_legacy_src *src)
{
   if (src->is_ssa) {
      if (nir_def_is_const(src->ssa))
         return ntr_get_load_const_src(c, nir_def_as_load_const(src->ssa));

      return c->ssa_temp[src->ssa->index];
   } else {
      struct ureg_dst reg_temp = c->reg_temp[src->reg.handle->index];
      reg_temp.Index += src->reg.base_offset;

      if (src->reg.indirect) {
         struct ureg_src offset = ntr_get_src(c, nir_src_for_ssa(src->reg.indirect));
         return ureg_src_indirect(ureg_src(reg_temp), ntr_reladdr(c, offset, 0));
      } else {
         return ureg_src(reg_temp);
      }
   }
}

static struct ureg_src
ntr_get_src(struct ntr_compile *c, nir_src src)
{
   nir_legacy_src chased = nir_legacy_chase_src(&src);
   return ntr_get_chased_src(c, &chased);
}

static struct ureg_src
ntr_get_alu_src(struct ntr_compile *c, nir_alu_instr *instr, int i)
{
   /* We only support 32-bit float modifiers.  The only other modifier type
    * officially supported by TGSI is 32-bit integer negates, but even those are
    * broken on virglrenderer, so skip lowering all integer and f64 float mods.
    *
    * The lower_fabs requests that we not have native source modifiers
    * for fabs, and instead emit MAX(a,-a) for nir_op_fabs.
    */
   nir_legacy_alu_src src = nir_legacy_chase_alu_src(&instr->src[i], !c->lower_fabs);
   struct ureg_src usrc = ntr_get_chased_src(c, &src.src);

   usrc = ureg_swizzle(usrc, src.swizzle[0], src.swizzle[1], src.swizzle[2], src.swizzle[3]);

   if (src.fabs)
      usrc = ureg_abs(usrc);
   if (src.fneg)
      usrc = ureg_negate(usrc);

   return usrc;
}

/* Reswizzles a source so that the unset channels in the write mask still refer
 * to one of the channels present in the write mask.
 */
static struct ureg_src
ntr_swizzle_for_write_mask(struct ureg_src src, uint32_t write_mask)
{
   assert(write_mask);
   int first_chan = ffs(write_mask) - 1;
   return ureg_swizzle(src, (write_mask & TGSI_WRITEMASK_X) ? TGSI_SWIZZLE_X : first_chan,
                       (write_mask & TGSI_WRITEMASK_Y) ? TGSI_SWIZZLE_Y : first_chan,
                       (write_mask & TGSI_WRITEMASK_Z) ? TGSI_SWIZZLE_Z : first_chan,
                       (write_mask & TGSI_WRITEMASK_W) ? TGSI_SWIZZLE_W : first_chan);
}

static struct ureg_dst
ntr_get_ssa_def_decl(struct ntr_compile *c, nir_def *ssa)
{
   uint32_t writemask;
   /* Fix writemask for nir_intrinsic_load_ubo_vec4 according to uses. */
   if (nir_def_is_intrinsic(ssa) &&
       nir_def_as_intrinsic(ssa)->intrinsic == nir_intrinsic_load_ubo_vec4)
      writemask = nir_def_components_read(ssa);
   else
      writemask = BITSET_MASK(ssa->num_components);

   struct ureg_dst dst;
   if (!ntr_try_store_ssa_in_tgsi_output(c, &dst, ssa))
      dst = ntr_temp(c);

   c->ssa_temp[ssa->index] = ntr_swizzle_for_write_mask(ureg_src(dst), writemask);

   return ureg_writemask(dst, writemask);
}

static struct ureg_dst
ntr_get_chased_dest_decl(struct ntr_compile *c, nir_legacy_dest *dest)
{
   if (dest->is_ssa)
      return ntr_get_ssa_def_decl(c, dest->ssa);
   else
      return c->reg_temp[dest->reg.handle->index];
}

static struct ureg_dst
ntr_get_chased_dest(struct ntr_compile *c, nir_legacy_dest *dest)
{
   struct ureg_dst dst = ntr_get_chased_dest_decl(c, dest);

   if (!dest->is_ssa) {
      dst.Index += dest->reg.base_offset;

      if (dest->reg.indirect) {
         struct ureg_src offset = ntr_get_src(c, nir_src_for_ssa(dest->reg.indirect));
         dst = ureg_dst_indirect(dst, ntr_reladdr(c, offset, 0));
      }
   }

   return dst;
}

static struct ureg_dst
ntr_get_dest(struct ntr_compile *c, nir_def *def)
{
   nir_legacy_dest chased = nir_legacy_chase_dest(def);
   return ntr_get_chased_dest(c, &chased);
}

static struct ureg_dst
ntr_get_alu_dest(struct ntr_compile *c, nir_def *def)
{
   nir_legacy_alu_dest chased = nir_legacy_chase_alu_dest(def);
   struct ureg_dst dst = ntr_get_chased_dest(c, &chased.dest);

   if (chased.fsat)
      dst.Saturate = true;

   /* Only registers get write masks */
   if (chased.dest.is_ssa)
      return dst;

   return ureg_writemask(dst, chased.write_mask);
}

/* For an SSA dest being populated by a constant src, replace the storage with
 * a copy of the ureg_src.
 */
static void
ntr_store_def(struct ntr_compile *c, nir_def *def, struct ureg_src src)
{
   if (!src.Indirect && !src.DimIndirect) {
      switch (src.File) {
      case TGSI_FILE_IMMEDIATE:
      case TGSI_FILE_INPUT:
      case TGSI_FILE_CONSTANT:
         c->ssa_temp[def->index] = src;
         return;
      }
   }

   ntr_MOV(c, ntr_get_ssa_def_decl(c, def), src);
}

static void
ntr_store(struct ntr_compile *c, nir_def *def, struct ureg_src src)
{
   nir_legacy_dest chased = nir_legacy_chase_dest(def);

   if (chased.is_ssa)
      ntr_store_def(c, chased.ssa, src);
   else {
      struct ureg_dst dst = ntr_get_chased_dest(c, &chased);
      ntr_MOV(c, dst, src);
   }
}

static void
ntr_emit_scalar(struct ntr_compile *c, rc_opcode op, struct ureg_dst dst,
                struct ureg_src src0, struct ureg_src src1)
{
   unsigned i;

   /* POW is the only 2-operand scalar op. */
   if (op != RC_OPCODE_POW)
      src1 = src0;

   for (i = 0; i < 4; i++) {
      if (dst.WriteMask & (1 << i)) {
         ntr_insn(c, op, ureg_writemask(dst, 1 << i), ureg_scalar(src0, i),
                  ureg_scalar(src1, i), ureg_src_undef(), ureg_src_undef());
      }
   }
}

static void
ntr_emit_alu(struct ntr_compile *c, nir_alu_instr *instr)
{
   struct ureg_src src[4];
   struct ureg_dst dst;
   unsigned i;
   int num_srcs = nir_op_infos[instr->op].num_inputs;

   /* Don't try to translate folded fsat since their source won't be valid */
   if (instr->op == nir_op_fsat && nir_legacy_fsat_folds(instr))
      return;

   c->precise = nir_alu_instr_is_exact(instr);

   assert(num_srcs <= ARRAY_SIZE(src));
   for (i = 0; i < num_srcs; i++)
      src[i] = ntr_get_alu_src(c, instr, i);
   for (; i < ARRAY_SIZE(src); i++)
      src[i] = ureg_src_undef();

   dst = ntr_get_alu_dest(c, &instr->def);

   static const rc_opcode op_map[] = {
      [nir_op_mov] = RC_OPCODE_MOV,

      [nir_op_fdot2_replicated] = RC_OPCODE_DP2,
      [nir_op_fdot3_replicated] = RC_OPCODE_DP3,
      [nir_op_fdot4_replicated] = RC_OPCODE_DP4,
      [nir_op_ffract] = RC_OPCODE_FRC,
      [nir_op_fround_even] = RC_OPCODE_ROUND,

      [nir_op_slt] = RC_OPCODE_SLT,
      [nir_op_sge] = RC_OPCODE_SGE,
      [nir_op_seq] = RC_OPCODE_SEQ,
      [nir_op_sne] = RC_OPCODE_SNE,

      [nir_op_fadd] = RC_OPCODE_ADD,
      [nir_op_fmul] = RC_OPCODE_MUL,

      [nir_op_fmin] = RC_OPCODE_MIN,
      [nir_op_fmax] = RC_OPCODE_MAX,
      [nir_op_fmad] = RC_OPCODE_MAD,
   };

   if (instr->op < ARRAY_SIZE(op_map) && op_map[instr->op] > 0) {
      /* The normal path for NIR to TGSI ALU op translation */
      ntr_insn(c, op_map[instr->op], dst, src[0], src[1], src[2], src[3]);
   } else {
      /* Special cases for NIR to TGSI ALU op translation. */

      /* TODO: Use something like the ntr_store() path for the MOV calls so we
       * don't emit extra MOVs for swizzles/srcmods of inputs/const/imm.
       */

      switch (instr->op) {
      case nir_op_fabs:
         /* Try to eliminate */
         if (!c->lower_fabs && nir_legacy_float_mod_folds(instr))
            break;

         if (c->lower_fabs)
            ntr_MAX(c, dst, src[0], ureg_negate(src[0]));
         else
            ntr_MOV(c, dst, ureg_abs(src[0]));
         break;

      case nir_op_fsat:
         ntr_MOV(c, ureg_saturate(dst), src[0]);
         break;

      case nir_op_fneg:
         /* Try to eliminate */
         if (nir_legacy_float_mod_folds(instr))
            break;

         ntr_MOV(c, dst, ureg_negate(src[0]));
         break;

         /* NOTE: TGSI 32-bit math ops have the old "one source channel
          * replicated to all dst channels" behavior, while 64 is normal mapping
          * of src channels to dst.
          */
      case nir_op_frcp:
         ntr_emit_scalar(c, RC_OPCODE_RCP, dst, src[0], ureg_src_undef());
         break;

      case nir_op_frsq:
         ntr_emit_scalar(c, RC_OPCODE_RSQ, dst, src[0], ureg_src_undef());
         break;

      case nir_op_fexp2:
         ntr_emit_scalar(c, RC_OPCODE_EX2, dst, src[0], ureg_src_undef());
         break;

      case nir_op_flog2:
         ntr_emit_scalar(c, RC_OPCODE_LG2, dst, src[0], ureg_src_undef());
         break;

      case nir_op_fsin:
         ntr_emit_scalar(c, RC_OPCODE_SIN, dst, src[0], ureg_src_undef());
         break;

      case nir_op_fcos:
         ntr_emit_scalar(c, RC_OPCODE_COS, dst, src[0], ureg_src_undef());
         break;

      case nir_op_fsub:
         ntr_ADD(c, dst, src[0], ureg_negate(src[1]));
         break;

      case nir_op_fmod:
         UNREACHABLE("should be handled by .lower_fmod = true");
         break;

      case nir_op_fpow:
         ntr_emit_scalar(c, RC_OPCODE_POW, dst, src[0], src[1]);
         break;

      case nir_op_fcsel:
         /* Implement this as CMP(-abs(src0), src1, src2). */
         ntr_CMP(c, dst, ureg_negate(ureg_abs(src[0])), src[1], src[2]);
         break;

      case nir_op_fcsel_gt:
         ntr_CMP(c, dst, ureg_negate(src[0]), src[1], src[2]);
         break;

      case nir_op_fcsel_ge:
         /* Implement this as if !(src0 < 0.0) was identical to src0 >= 0.0. */
         ntr_CMP(c, dst, src[0], src[2], src[1]);
         break;

      case nir_op_vec4:
      case nir_op_vec3:
      case nir_op_vec2:
         UNREACHABLE("covered by nir_lower_vec_to_movs()");

      default:
         fprintf(stderr, "Unknown NIR opcode: %s\n", nir_op_infos[instr->op].name);
         UNREACHABLE("Unknown NIR opcode");
      }
   }

   c->precise = false;
}

static struct ureg_src
ntr_ureg_src_indirect(struct ntr_compile *c, struct ureg_src usrc, nir_src src, int addr_reg)
{
   if (nir_src_is_const(src)) {
      usrc.Index += ntr_src_as_uint(c, src);
      return usrc;
   } else {
      return ureg_src_indirect(usrc, ntr_reladdr(c, ntr_get_src(c, src), addr_reg));
   }
}

static struct ureg_dst
ntr_ureg_dst_indirect(struct ntr_compile *c, struct ureg_dst dst, nir_src src)
{
   if (nir_src_is_const(src)) {
      dst.Index += ntr_src_as_uint(c, src);
      return dst;
   } else {
      return ureg_dst_indirect(dst, ntr_reladdr(c, ntr_get_src(c, src), 0));
   }
}

static struct ureg_dst
ntr_ureg_dst_dimension_indirect(struct ntr_compile *c, struct ureg_dst udst, nir_src src)
{
   if (nir_src_is_const(src)) {
      return ureg_dst_dimension(udst, ntr_src_as_uint(c, src));
   } else {
      return ureg_dst_dimension_indirect(udst, ntr_reladdr(c, ntr_get_src(c, src), 1), 0);
   }
}
/* Some load operations in NIR will have a fractional offset that we need to
 * swizzle down before storing to the result register.
 */
static struct ureg_src
ntr_shift_by_frac(struct ureg_src src, unsigned frac, unsigned num_components)
{
   return ureg_swizzle(src, frac, frac + MIN2(num_components - 1, 1),
                       frac + MIN2(num_components - 1, 2), frac + MIN2(num_components - 1, 3));
}

static void
ntr_emit_load_ubo(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   struct ureg_src src = ureg_src_register(TGSI_FILE_CONSTANT, 0);

   /* r300 only exposes a single UBO and any indirect UBO array indexing
    * has been lowered before we get here. */
   assert(nir_src_is_const(instr->src[0]));
   src = ureg_src_dimension(src, ntr_src_as_uint(c, instr->src[0]));

   /* !pipe_caps.load_constbuf: Just emit it as a vec4 reference to the const
    * file.
    */
   src.Index = nir_intrinsic_base(instr);

   if (nir_src_is_const(instr->src[1])) {
      src.Index += ntr_src_as_uint(c, instr->src[1]);
   } else {
      src = ureg_src_indirect(src, ntr_reladdr(c, ntr_get_src(c, instr->src[1]), 0));
   }

   int start_component = nir_intrinsic_component(instr);

   src = ntr_shift_by_frac(src, start_component, instr->num_components);

   ntr_store(c, &instr->def, src);
}

static void
ntr_emit_load_input(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   uint32_t frac = nir_intrinsic_component(instr);
   uint32_t num_components = instr->num_components;
   unsigned base = nir_intrinsic_base(instr);
   struct ureg_src input;
   nir_io_semantics semantics = nir_intrinsic_io_semantics(instr);

   if (c->s->info.stage == MESA_SHADER_FRAGMENT)
      ntr_read_input_output(c, semantics.location, base);

   if (c->s->info.stage == MESA_SHADER_VERTEX) {
      input = ureg_DECL_vs_input(c->ureg, base);
      for (int i = 1; i < semantics.num_slots; i++)
         ureg_DECL_vs_input(c->ureg, base + i);
   } else {
      input = c->input_index_map[base];
   }

   input = ntr_shift_by_frac(input, frac, num_components);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
      input = ntr_ureg_src_indirect(c, input, instr->src[0], 0);
      ntr_store(c, &instr->def, input);
      break;

   case nir_intrinsic_load_interpolated_input: {
      input = ntr_ureg_src_indirect(c, input, instr->src[1], 0);

      nir_intrinsic_instr *bary_instr = nir_def_as_intrinsic(instr->src[0].ssa);

      switch (bary_instr->intrinsic) {
      case nir_intrinsic_load_barycentric_pixel:
         /* The barycentric load matches the interpolation on the input
          * declaration, so we can use it directly.
          */
         ntr_store(c, &instr->def, input);
         break;

      case nir_intrinsic_load_barycentric_centroid:
         /* On r300 interpolation is fixed by the rasterizer state; the NIR
          * lowering pairs centroid intrinsics with centroid-declared inputs,
          * so we never need an explicit INTERP instruction. */
         ntr_store(c, &instr->def, input);
         break;

      default:
         UNREACHABLE("bad barycentric interp intrinsic\n");
      }
      break;
   }

   default:
      UNREACHABLE("bad load input intrinsic\n");
   }
}

static void
ntr_emit_store_output(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   struct ureg_src src = ntr_get_src(c, instr->src[0]);

   if (src.File == TGSI_FILE_OUTPUT) {
      /* If our src is the output file, that's an indication that we were able
       * to emit the output stores in the generating instructions and we have
       * nothing to do here.
       */
      return;
   }

   uint32_t frac;
   struct ureg_dst out = ntr_output_decl(c, instr, &frac);

   out = ntr_ureg_dst_indirect(c, out, instr->src[1]);

   uint8_t swizzle[4] = {0, 0, 0, 0};
   for (int i = frac; i < 4; i++) {
      if (out.WriteMask & (1 << i))
         swizzle[i] = i - frac;
   }

   src = ureg_swizzle(src, swizzle[0], swizzle[1], swizzle[2], swizzle[3]);

   ntr_MOV(c, out, src);
}

static void
ntr_emit_load_output(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   /* r300 has no GS/tess stages and doesn't expose framebuffer fetch,
    * so the only callers of nir_intrinsic_load_output are gone.
    */
   UNREACHABLE("load_output not supported on r300");
}

static void
ntr_emit_intrinsic(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_vec4:
      ntr_emit_load_ubo(c, instr);
      break;

   case nir_intrinsic_load_frag_coord:
   case nir_intrinsic_load_point_coord:
   case nir_intrinsic_load_front_face:
      UNREACHABLE("system value should have been lowered to a varying");
      break;

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
      ntr_emit_load_input(c, instr);
      break;

   case nir_intrinsic_store_output:
      ntr_emit_store_output(c, instr);
      break;

   case nir_intrinsic_load_output:
      ntr_emit_load_output(c, instr);
      break;

   case nir_intrinsic_terminate:
      ntr_KILL(c);
      break;

   case nir_intrinsic_terminate_if: {
      struct ureg_src cond = ureg_scalar(ntr_get_src(c, instr->src[0]), 0);
      /* For !native_integers, the bool got lowered to 1.0 or 0.0. */
      ntr_KILL_IF(c, ureg_negate(cond));
      break;
   }
      /* In TGSI we don't actually generate the barycentric coords, and
       * emit the corresponding INTERP_CENTROID instruction in
       * ntr_emit_load_input. The barycentric loads themselves are
       * therefore consumed there.
       */
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
      break;

   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_coarse:
      ntr_DDX(c, ntr_get_dest(c, &instr->def), ntr_get_src(c, instr->src[0]));
      return;
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_coarse:
      ntr_DDY(c, ntr_get_dest(c, &instr->def), ntr_get_src(c, instr->src[0]));
      return;

   case nir_intrinsic_decl_reg:
   case nir_intrinsic_load_reg:
   case nir_intrinsic_load_reg_indirect:
   case nir_intrinsic_store_reg:
   case nir_intrinsic_store_reg_indirect:
      /* fully consumed */
      break;

   default:
      fprintf(stderr, "Unknown intrinsic: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
      break;
   }
}

struct ntr_tex_operand_state {
   struct ureg_src srcs[4];
   unsigned i;
};

static void
ntr_push_tex_arg(struct ntr_compile *c, nir_tex_instr *instr, nir_tex_src_type tex_src_type,
                 struct ntr_tex_operand_state *s)
{
   int tex_src = nir_tex_instr_src_index(instr, tex_src_type);
   if (tex_src < 0)
      return;

   nir_src *src = &instr->src[tex_src].src;
   s->srcs[s->i++] = ntr_get_src(c, *src);
}

static void
ntr_emit_texture(struct ntr_compile *c, nir_tex_instr *instr)
{
   struct ureg_dst dst = ntr_get_dest(c, &instr->def);
   assert(!instr->is_shadow);
   enum tgsi_texture_type target =
      tgsi_texture_type_from_sampler_dim(instr->sampler_dim, instr->is_array);
   unsigned tex_opcode;

   int tex_handle_src = nir_tex_instr_src_index(instr, nir_tex_src_texture_handle);
   int sampler_handle_src = nir_tex_instr_src_index(instr, nir_tex_src_sampler_handle);

   struct ureg_src sampler;
   if (tex_handle_src >= 0 && sampler_handle_src >= 0) {
      /* It seems we can't get separate tex/sampler on GL, just use one of the handles */
      sampler = ntr_get_src(c, instr->src[tex_handle_src].src);
      assert(nir_tex_instr_src_index(instr, nir_tex_src_sampler_offset) == -1);
   } else {
      assert(tex_handle_src == -1 && sampler_handle_src == -1);
      sampler = ureg_src_register(TGSI_FILE_SAMPLER, instr->sampler_index);
      int sampler_src = nir_tex_instr_src_index(instr, nir_tex_src_sampler_offset);
      if (sampler_src >= 0) {
         struct ureg_src reladdr = ntr_get_src(c, instr->src[sampler_src].src);
         sampler = ureg_src_indirect(sampler, ntr_reladdr(c, reladdr, 2));
      }
   }

   switch (instr->op) {
   case nir_texop_tex:
      if (nir_tex_instr_src_size(instr, nir_tex_instr_src_index(instr, nir_tex_src_backend1)) >
          MAX2(instr->coord_components, 2) + instr->is_shadow)
         tex_opcode = RC_OPCODE_TXP;
      else
         tex_opcode = RC_OPCODE_TEX;
      break;
   case nir_texop_txl:
      tex_opcode = RC_OPCODE_TXL;
      break;
   case nir_texop_txb:
      tex_opcode = RC_OPCODE_TXB;
      break;
   case nir_texop_txd:
      tex_opcode = RC_OPCODE_TXD;
      break;
   default:
      UNREACHABLE("unsupported tex op");
   }

   struct ntr_tex_operand_state s = {.i = 0};
   ntr_push_tex_arg(c, instr, nir_tex_src_backend1, &s);
   ntr_push_tex_arg(c, instr, nir_tex_src_backend2, &s);

   /* The RC backend doesn't have TEX2/TXB2/TXL2-style opcodes; the
    * shadow comparator that would need a second backend slot is
    * already lowered before nir_to_rc by nir_lower_tex_shadow. */
   assert(s.i <= 1);

   if (instr->op == nir_texop_txd) {
      /* Derivs appear in their own src args */
      int ddx = nir_tex_instr_src_index(instr, nir_tex_src_ddx);
      int ddy = nir_tex_instr_src_index(instr, nir_tex_src_ddy);
      s.srcs[s.i++] = ntr_get_src(c, instr->src[ddx].src);
      s.srcs[s.i++] = ntr_get_src(c, instr->src[ddy].src);
   }

   struct ureg_dst tex_dst;
   if (instr->op == nir_texop_query_levels)
      tex_dst = ureg_writemask(ntr_temp(c), TGSI_WRITEMASK_W);
   else
      tex_dst = dst;

   while (s.i < 4)
      s.srcs[s.i++] = ureg_src_undef();

   struct ntr_insn *insn =
      ntr_insn(c, tex_opcode, tex_dst, s.srcs[0], s.srcs[1], s.srcs[2], s.srcs[3]);
   insn->tex_target = target;
   insn->tex_sampler = sampler;
   insn->is_tex = true;

   int tex_offset_src = nir_tex_instr_src_index(instr, nir_tex_src_offset);
   if (tex_offset_src >= 0) {
      struct ureg_src offset = ntr_get_src(c, instr->src[tex_offset_src].src);

      insn->tex_offset[0].File = offset.File;
      insn->tex_offset[0].Index = offset.Index;
      insn->tex_offset[0].SwizzleX = offset.SwizzleX;
      insn->tex_offset[0].SwizzleY = offset.SwizzleY;
      insn->tex_offset[0].SwizzleZ = offset.SwizzleZ;
      insn->tex_offset[0].Padding = 0;
   }

   if (nir_tex_instr_has_explicit_tg4_offsets(instr)) {
      for (uint8_t i = 0; i < 4; ++i) {
         struct ureg_src imm =
            ureg_imm2i(c->ureg, instr->tg4_offsets[i][0], instr->tg4_offsets[i][1]);
         insn->tex_offset[i].File = imm.File;
         insn->tex_offset[i].Index = imm.Index;
         insn->tex_offset[i].SwizzleX = imm.SwizzleX;
         insn->tex_offset[i].SwizzleY = imm.SwizzleY;
         insn->tex_offset[i].SwizzleZ = imm.SwizzleZ;
      }
   }

   if (instr->op == nir_texop_query_levels)
      ntr_MOV(c, dst, ureg_scalar(ureg_src(tex_dst), 3));
}

static void
ntr_emit_jump(struct ntr_compile *c, nir_jump_instr *jump)
{
   switch (jump->type) {
   case nir_jump_break:
      ntr_BRK(c);
      break;

   case nir_jump_continue:
      ntr_CONT(c);
      break;

   default:
      fprintf(stderr, "Unknown jump instruction: ");
      nir_print_instr(&jump->instr, stderr);
      fprintf(stderr, "\n");
      abort();
   }
}

static void
ntr_emit_ssa_undef(struct ntr_compile *c, nir_undef_instr *instr)
{
   /* Nothing to do but make sure that we have some storage to deref. */
   (void)ntr_get_ssa_def_decl(c, &instr->def);
}

static void
ntr_emit_instr(struct ntr_compile *c, nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_deref:
      /* ignored, will be walked by nir_intrinsic_image_*_deref. */
      break;

   case nir_instr_type_alu:
      ntr_emit_alu(c, nir_instr_as_alu(instr));
      break;

   case nir_instr_type_intrinsic:
      ntr_emit_intrinsic(c, nir_instr_as_intrinsic(instr));
      break;

   case nir_instr_type_load_const:
      /* Nothing to do here, as load consts are done directly from
       * ntr_get_src() (since many constant NIR srcs will often get folded
       * directly into a register file index instead of as a TGSI src).
       */
      break;

   case nir_instr_type_tex:
      ntr_emit_texture(c, nir_instr_as_tex(instr));
      break;

   case nir_instr_type_jump:
      ntr_emit_jump(c, nir_instr_as_jump(instr));
      break;

   case nir_instr_type_undef:
      ntr_emit_ssa_undef(c, nir_instr_as_undef(instr));
      break;

   default:
      fprintf(stderr, "Unknown NIR instr type: ");
      nir_print_instr(instr, stderr);
      fprintf(stderr, "\n");
      abort();
   }
}

static void
ntr_emit_if(struct ntr_compile *c, nir_if *if_stmt)
{
   ntr_IF(c, c->if_cond);

   ntr_emit_cf_list(c, &if_stmt->then_list);

   if (!nir_cf_list_is_empty_block(&if_stmt->else_list)) {
      ntr_ELSE(c);
      ntr_emit_cf_list(c, &if_stmt->else_list);
   }

   ntr_ENDIF(c);
}

static void
ntr_emit_loop(struct ntr_compile *c, nir_loop *loop)
{
   assert(!nir_loop_has_continue_construct(loop));
   ntr_BGNLOOP(c);
   ntr_emit_cf_list(c, &loop->body);
   ntr_ENDLOOP(c);
}

static void
ntr_emit_block(struct ntr_compile *c, nir_block *block)
{
   struct ntr_block *ntr_block = ntr_block_from_nir(c, block);
   c->cur_block = ntr_block;

   nir_foreach_instr (instr, block) {
      ntr_emit_instr(c, instr);
   }

   /* Set up the if condition for ntr_emit_if(), which we have to do before
    * freeing up the temps (the "if" is treated as inside the block for liveness
    * purposes, despite not being an instruction)
    *
    * Note that, while IF and UIF are supposed to look at only .x, virglrenderer
    * looks at all of .xyzw.  No harm in working around the bug.
    */
   nir_if *nif = nir_block_get_following_if(block);
   if (nif)
      c->if_cond = ureg_scalar(ntr_get_src(c, nif->condition), TGSI_SWIZZLE_X);
}

static void
ntr_emit_cf_list(struct ntr_compile *c, struct exec_list *list)
{
   foreach_list_typed (nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         ntr_emit_block(c, nir_cf_node_as_block(node));
         break;

      case nir_cf_node_if:
         ntr_emit_if(c, nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         ntr_emit_loop(c, nir_cf_node_as_loop(node));
         break;

      default:
         UNREACHABLE("unknown CF type");
      }
   }
}

static void
ntr_translate_dst(struct ntr_compile *c, struct rc_dst_register *rc_dst,
                  struct ureg_dst src)
{
   rc_dst->File = rc_translate_register_file(src.File);
   rc_dst->Index = src.Index;
   rc_dst->WriteMask = src.WriteMask;
   if (src.Indirect)
      rc_error(c->compiler,
               "r300: Relative addressing of destination operands is unsupported.\n");
}

static void
ntr_translate_src(struct ntr_compile *c, struct rc_src_register *rc_src,
                  struct ureg_src src)
{
   rc_src->File = rc_translate_register_file(src.File);
   int index = src.Index;
   if (src.File == TGSI_FILE_IMMEDIATE)
      index += c->immediate_offset;
   /* Negative offsets to relative addressing should have been lowered in NIR */
   assert(index >= 0);
   if (index >= RC_REGISTER_MAX_INDEX)
      rc_error(c->compiler, "r300: Register index too high.\n");
   rc_src->Index = index;
   rc_src->RelAddr = src.Indirect;
   rc_src->Swizzle = src.SwizzleX;
   rc_src->Swizzle |= src.SwizzleY << 3;
   rc_src->Swizzle |= src.SwizzleZ << 6;
   rc_src->Swizzle |= src.SwizzleW << 9;
   rc_src->Abs = src.Absolute;
   rc_src->Negate = src.Negate ? RC_MASK_XYZW : 0;
}

static void
ntr_emit_block_rc(struct ntr_compile *c, struct nir_block *block)
{
   struct ntr_block *ntr_block = ntr_block_from_nir(c, block);

   util_dynarray_foreach (&ntr_block->insns, struct ntr_insn, insn) {
      const struct rc_opcode_info *opcode_info = rc_get_opcode_info(insn->opcode);
      struct rc_instruction *rc_insn =
         rc_insert_new_instruction(c->compiler, c->compiler->Program.Instructions.Prev);

      rc_insn->U.I.Opcode = insn->opcode;

      if (opcode_info->HasDstReg) {
         rc_insn->U.I.SaturateMode = rc_translate_saturate(insn->dst[0].Saturate);
         ntr_translate_dst(c, &rc_insn->U.I.DstReg, insn->dst[0]);
      }

      for (unsigned i = 0; i < opcode_info->NumSrcRegs; i++)
         ntr_translate_src(c, &rc_insn->U.I.SrcReg[i], insn->src[i]);

      if (insn->is_tex) {
         assert(insn->tex_sampler.File == TGSI_FILE_SAMPLER);
         rc_insn->U.I.TexSrcUnit = insn->tex_sampler.Index;
         rc_insn->U.I.TexSrcTarget = rc_translate_tex_target(insn->tex_target);
         rc_insn->U.I.TexSwizzle = RC_SWIZZLE_XYZW;
      }
   }
}

static void
ntr_emit_if_rc(struct ntr_compile *c, nir_if *if_stmt)
{
   /* The block before this if/else has already emitted the IF opcode into
    * our ntr_insn list. RC is structural so it doesn't need label fixups. */
   ntr_emit_cf_list_ureg(c, &if_stmt->then_list);
   ntr_emit_cf_list_ureg(c, &if_stmt->else_list);
}

static void
ntr_emit_cf_list_ureg(struct ntr_compile *c, struct exec_list *list)
{
   foreach_list_typed (nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         ntr_emit_block_rc(c, nir_cf_node_as_block(node));
         break;

      case nir_cf_node_if:
         ntr_emit_if_rc(c, nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         /* GLSL-to-TGSI never set the begin/end labels to anything, even though nvfx
          * does reference BGNLOOP's.  Follow the former behavior unless something comes up
          * with a need.
          */
         ntr_emit_cf_list_ureg(c, &nir_cf_node_as_loop(node)->body);
         break;

      default:
         UNREACHABLE("unknown CF type");
      }
   }
}

static void
ntr_add_constants(struct ntr_compile *c)
{
   /* Add UBO constants first (they show up before immediates in the
    * compiler's constants table; the immediate offset compensates for
    * this). */
   for (unsigned i = 0; i < c->ubo_size; i++) {
      struct rc_constant constant;
      memset(&constant, 0, sizeof(constant));
      constant.Type = RC_CONSTANT_EXTERNAL;
      constant.UseMask = RC_MASK_XYZW;
      constant.u.External = i;
      rc_constants_add(&c->compiler->Program.Constants, &constant);
   }
   c->immediate_offset = c->compiler->Program.Constants.Count;

   util_dynarray_foreach (&c->immediates, struct ntr_immediate, imm) {
      struct rc_constant constant;
      constant.Type = RC_CONSTANT_IMMEDIATE;
      constant.UseMask = RC_MASK_XYZW;
      for (unsigned j = 0; j < 4; j++)
         constant.u.Immediate[j] = imm->values[j];
      rc_constants_add(&c->compiler->Program.Constants, &constant);
   }
}

static void
ntr_emit_impl(struct ntr_compile *c, nir_function_impl *impl)
{
   c->impl = impl;

   c->ssa_temp = rzalloc_array(c, struct ureg_src, impl->ssa_alloc);
   c->reg_temp = rzalloc_array(c, struct ureg_dst, impl->ssa_alloc);

   /* Set up the struct ntr_blocks to put insns in */
   c->blocks = _mesa_pointer_hash_table_create(c);
   nir_foreach_block (block, impl) {
      struct ntr_block *ntr_block = rzalloc(c->blocks, struct ntr_block);
      util_dynarray_init(&ntr_block->insns, ntr_block);
      _mesa_hash_table_insert(c->blocks, block, ntr_block);
   }

   ntr_setup_registers(c);

   c->cur_block = ntr_block_from_nir(c, nir_start_block(impl));
   ntr_setup_inputs(c);
   ntr_setup_outputs(c);
   ntr_setup_uniforms(c);

   /* Emit the ntr insns */
   ntr_emit_cf_list(c, &impl->body);

   ntr_allocate_regs_unoptimized(c, impl);

   /* Add constants and emit RC instructions directly. */
   ntr_add_constants(c);
   ntr_emit_cf_list_ureg(c, &impl->body);

   ralloc_free(c->liveness);
   c->liveness = NULL;
}

static int
type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

/* Allow vectorizing of ALU instructions.
 */
static uint8_t
ntr_should_vectorize_instr(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_alu)
      return 0;

   return 4;
}

static bool
ntr_should_vectorize_io(unsigned align, unsigned bit_size, unsigned num_components,
                        unsigned high_offset, nir_intrinsic_instr *low, nir_intrinsic_instr *high,
                        void *data)
{
   if (bit_size != 32)
      return false;

   /* Our offset alignment should always be at least 4 bytes */
   if (align < 4)
      return false;

   /* No wrapping off the end of a TGSI reg.  We could do a bit better by
    * looking at low's actual offset.  XXX: With LOAD_CONSTBUF maybe we don't
    * need this restriction.
    */
   unsigned worst_start_component = align == 4 ? 3 : align / 4;
   if (worst_start_component + num_components > 4)
      return false;

   return true;
}

static nir_variable_mode
ntr_no_indirects_mask(nir_shader *s, struct pipe_screen *screen)
{
   unsigned pipe_stage = s->info.stage;
   unsigned indirect_mask = nir_var_shader_in | nir_var_shader_out;

   if (!screen->shader_caps[pipe_stage].indirect_temp_addr) {
      indirect_mask |= nir_var_function_temp;
   }

   return indirect_mask;
}

struct ntr_lower_tex_state {
   nir_scalar channels[8];
   unsigned i;
};

static void
nir_to_rc_lower_tex_instr_arg(nir_builder *b, nir_tex_instr *instr, nir_tex_src_type tex_src_type,
                              struct ntr_lower_tex_state *s)
{
   int tex_src = nir_tex_instr_src_index(instr, tex_src_type);
   if (tex_src < 0)
      return;

   nir_def *def = instr->src[tex_src].src.ssa;
   for (int i = 0; i < def->num_components; i++) {
      s->channels[s->i++] = nir_get_scalar(def, i);
   }

   nir_tex_instr_remove_src(instr, tex_src);
}

/**
 * Merges together a vec4 of tex coordinate/compare/bias/lod into a backend tex
 * src.  This lets NIR handle the coalescing of the vec4 rather than trying to
 * manage it on our own, and may lead to more vectorization.
 */
static bool
nir_to_rc_lower_tex_instr(nir_builder *b, nir_tex_instr *tex, void *data)
{
   if (nir_tex_instr_src_index(tex, nir_tex_src_coord) < 0)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   struct ntr_lower_tex_state s = {0};

   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_coord, &s);
   /* We always have at least two slots for the coordinate, even on 1D. */
   s.i = MAX2(s.i, 2);

   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_comparator, &s);
   s.i = MAX2(s.i, 3);

   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_bias, &s);

   /* XXX: LZ */
   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_lod, &s);
   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_projector, &s);
   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_ms_index, &s);

   /* No need to pack undefs in unused channels of the tex instr */
   while (!s.channels[s.i - 1].def)
      s.i--;

   /* Instead of putting undefs in the unused slots of the vecs, just put in
    * another used channel.  Otherwise, we'll get unnecessary moves into
    * registers.
    */
   assert(s.channels[0].def != NULL);
   for (int i = 1; i < s.i; i++) {
      if (!s.channels[i].def)
         s.channels[i] = s.channels[0];
   }

   nir_tex_instr_add_src(tex, nir_tex_src_backend1, nir_vec_scalars(b, s.channels, MIN2(s.i, 4)));
   if (s.i > 4)
      nir_tex_instr_add_src(tex, nir_tex_src_backend2, nir_vec_scalars(b, &s.channels[4], s.i - 4));

   return true;
}

static bool
nir_to_rc_lower_tex(nir_shader *s)
{
   return nir_shader_tex_pass(s, nir_to_rc_lower_tex_instr,
                              nir_metadata_control_flow, NULL);
}

/* Lowers texture projectors if we can't do them as RC_OPCODE_TXP. */
static void
nir_to_rc_lower_txp(nir_shader *s)
{
   nir_lower_tex_options lower_tex_options = {
      .lower_txp = 0,
   };

   nir_foreach_block (block, nir_shader_get_entrypoint(s)) {
      nir_foreach_instr (instr, block) {
         if (instr->type != nir_instr_type_tex)
            continue;
         nir_tex_instr *tex = nir_instr_as_tex(instr);

         if (nir_tex_instr_src_index(tex, nir_tex_src_projector) < 0)
            continue;

         bool has_compare = nir_tex_instr_src_index(tex, nir_tex_src_comparator) >= 0;
         bool has_lod = nir_tex_instr_src_index(tex, nir_tex_src_lod) >= 0 ||
                        s->info.stage != MESA_SHADER_FRAGMENT;
         bool has_offset = nir_tex_instr_src_index(tex, nir_tex_src_offset) >= 0;

         /* We can do TXP for any tex (not txg) where we can fit all the
          * coordinates and comparator and projector in one vec4 without any
          * other modifiers to add on.
          *
          * nir_lower_tex() only handles the lowering on a sampler-dim basis, so
          * if we get any funny projectors then we just blow them all away.
          */
         if (tex->op != nir_texop_tex || has_lod || has_offset ||
             (tex->coord_components >= 3 && has_compare))
            lower_tex_options.lower_txp |= 1 << tex->sampler_dim;
      }
   }

   /* nir_lower_tex must be run even if no options are set, because we need the
    * LOD to be set for query_levels and for non-fragment shaders.
    */
   NIR_PASS(_, s, nir_lower_tex, &lower_tex_options);
}

/* There are some issues with the tgsi_texcoord = false support in the state
 * tracker, specifically we are still getting texcoords and pointcoord in some cases
 * and ocassionally (with fixed function shaders) there are some inconsistencies
 * like vs using generics and fs using texcoords. This function tries to fix it.
 * See https://gitlab.freedesktop.org/mesa/mesa/-/issues/12749 for more details.
 */
void
ntr_fixup_varying_slots(nir_shader *s, nir_variable_mode mode)
{
   if (s->info.name && !strcmp(s->info.name, "st/drawtex VS"))
      return;

   nir_foreach_variable_with_modes (var, s, mode) {
      if (var->data.location >= VARYING_SLOT_VAR0 && var->data.location < VARYING_SLOT_PATCH0) {
         var->data.location += 9;
      } else if (var->data.location == VARYING_SLOT_PNTC) {
         var->data.location = VARYING_SLOT_VAR8;
      } else if ((var->data.location >= VARYING_SLOT_TEX0) &&
                 (var->data.location <= VARYING_SLOT_TEX7)) {
         var->data.location += VARYING_SLOT_VAR0 - VARYING_SLOT_TEX0;
      }
   }
}

/**
 * Translates the NIR shader to RC instructions on the given compiler.
 *
 * This requires some lowering of the NIR shader to prepare it for
 * translation. We take ownership of the NIR shader passed; if you need to
 * keep the NIR, then pass us a clone.
 */
void
nir_to_rc(struct nir_shader *s, struct pipe_screen *screen,
          struct r300_fragment_program_external_state state,
          union r300_shader_code rc, struct radeon_compiler *compiler)
{
   struct ntr_compile *c;
   bool is_r500 = r300_screen(screen)->caps.is_r500;
   c = rzalloc(NULL, struct ntr_compile);
   c->screen = screen;
   c->compiler = compiler;
   c->lower_fabs = !is_r500 && s->info.stage == MESA_SHADER_VERTEX;
   util_dynarray_init(&c->immediates, c);
   for (unsigned i = 0; i < ARRAY_SIZE(c->fs_output_color_index); i++)
      c->fs_output_color_index[i] = -1;
   c->fs_output_depth_index = -1;
   for (unsigned i = 0; i < ARRAY_SIZE(c->fs_output_index); i++)
      c->fs_output_index[i] = -1;
   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      c->semantics = &rc.f->inputs;
   } else {
      c->semantics = &rc.v->outputs;
   }

   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      static const nir_lower_sysvals_to_varyings_options sysval_options = {
         .frag_coord = true,
         .point_coord = true,
         .front_face = true,
      };
      NIR_PASS(_, s, nir_lower_sysvals_to_varyings, &sysval_options);
   }

   ntr_fixup_varying_slots(s, s->info.stage == MESA_SHADER_FRAGMENT ? nir_var_shader_in : nir_var_shader_out);

   /* Lower array indexing on FS inputs.  Since we don't set
    * ureg->supports_any_inout_decl_range, the TGSI input decls will be split to
    * elements by ureg, and so dynamically indexing them would be invalid.
    * Ideally we would set that ureg flag based on
    * pipe_shader_caps.tgsi_any_inout_decl_range, but can't due to mesa/st
    * splitting NIR VS outputs to elements even if the FS doesn't get the
    * corresponding splitting, and virgl depends on TGSI across link boundaries
    * having matching declarations.
    */
   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, s, nir_lower_indirect_derefs_to_if_else_trees,
               nir_var_shader_in, UINT32_MAX);
      NIR_PASS(_, s, nir_remove_dead_variables, nir_var_shader_in, NULL);
   }

   NIR_PASS(_, s, nir_lower_io, nir_var_shader_in | nir_var_shader_out, type_size,
            nir_lower_io_use_interpolated_input_intrinsics);

   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      /* Shadow lowering. */
      int num_texture_states = state.sampler_state_count;
      if (num_texture_states > 0) {
         nir_lower_tex_shadow_swizzle tex_swizzle[PIPE_MAX_SHADER_SAMPLER_VIEWS];
         enum compare_func tex_compare_func[PIPE_MAX_SHADER_SAMPLER_VIEWS];

         for (unsigned i = 0; i < num_texture_states; i++) {
            tex_compare_func[i] = state.unit[i].texture_compare_func;
            tex_swizzle[i].swizzle_r = GET_SWZ(state.unit[i].texture_swizzle, 0);
            tex_swizzle[i].swizzle_g = GET_SWZ(state.unit[i].texture_swizzle, 1);
            tex_swizzle[i].swizzle_b = GET_SWZ(state.unit[i].texture_swizzle, 2);
            tex_swizzle[i].swizzle_a = GET_SWZ(state.unit[i].texture_swizzle, 3);
         }
         NIR_PASS(_, s, nir_lower_tex_shadow, num_texture_states, tex_compare_func,
                  tex_swizzle, true);
      }

      nir_to_rc_lower_txp(s);
      NIR_PASS(_, s, nir_to_rc_lower_tex);
   }

   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_constant_folding);
   } while (progress);

   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_algebraic_late);
      if (progress) {
         NIR_PASS(_, s, nir_opt_copy_prop);
         NIR_PASS(_, s, nir_opt_dce);
         NIR_PASS(_, s, nir_opt_cse);
      }
   } while (progress);

   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, s, r300_nir_prepare_presubtract);
   }

   NIR_PASS(_, s, nir_lower_int_to_float);
   NIR_PASS(_, s, nir_opt_copy_prop);
   NIR_PASS(_, s, r300_nir_post_integer_lowering);
   NIR_PASS(_, s, nir_lower_bool_to_float,
            is_r500 || s->info.stage == MESA_SHADER_FRAGMENT);
   /* bool_to_float generates MOVs for b2f32 that we want to clean up. */
   NIR_PASS(_, s, nir_opt_copy_prop);
   /* CSE cleanup after late ftrunc lowering. */
   NIR_PASS(_, s, nir_opt_cse);
   /* At this point we need to clean;
    *  a) fcsel_gt that come from the ftrunc lowering on R300,
    *  b) all flavours of fcsels that read three different temp sources on R500.
    */
   if (s->info.stage == MESA_SHADER_VERTEX) {
      if (is_r500)
         NIR_PASS(_, s, r300_nir_lower_fcsel_r500);
      else
         NIR_PASS(_, s, r300_nir_lower_fcsel_r300);
      NIR_PASS(_, s, r300_nir_lower_flrp);
   } else {
      NIR_PASS(_, s, r300_nir_lower_comparison_fs);
   }
   NIR_PASS(_, s, r300_nir_opt_algebraic_late);
   NIR_PASS(_, s, nir_opt_dce);
   NIR_PASS(_, s, nir_opt_shrink_vectors, false);
   NIR_PASS(_, s, nir_opt_dce);

   nir_move_options move_all = nir_move_const_undef | nir_move_load_ubo | nir_move_load_input | nir_move_load_frag_coord |
                               nir_move_comparisons | nir_move_copies | nir_move_load_ssbo;

   NIR_PASS(_, s, nir_opt_move, move_all);
   NIR_PASS(_, s, nir_move_vec_src_uses_to_dest, true);
   /* Late vectorizing after nir_move_vec_src_uses_to_dest helps instructions but
    * increases register usage. Testing shows this is beneficial only in VS.
    */
   if (s->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, s, nir_opt_vectorize, ntr_should_vectorize_instr, NULL);

   NIR_PASS(_, s, nir_convert_from_ssa, true, false);
   NIR_PASS(_, s, nir_lower_vec_to_regs, NULL, NULL);

   /* locals_to_reg_intrinsics will leave dead derefs that are good to clean up.
    */
   NIR_PASS(_, s, nir_lower_locals_to_regs, 32);
   NIR_PASS(_, s, nir_opt_dce);

   /* See comment in ntr_get_alu_src for supported modifiers */
   NIR_PASS(_, s, nir_legacy_trivialize, !c->lower_fabs);

   if (NIR_DEBUG(TGSI)) {
      fprintf(stderr, "NIR before translation to TGSI:\n");
      nir_print_shader(s, stderr);
   }

   c->s = s;
   c->ureg = ureg_create(s->info.stage);
   ureg_setup_shader_info(c->ureg, &s->info);
   if (s->info.use_legacy_math_rules && screen->caps.legacy_math_rules)
      ureg_property(c->ureg, TGSI_PROPERTY_LEGACY_MATH_RULES, 1);

   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      /* The draw module's polygon stipple layer doesn't respect the chosen
       * coordinate mode, so leave it as unspecified unless we're actually
       * reading the position in the shader already.  See
       * gl-2.1-polygon-stipple-fs on softpipe.
       */
      if ((s->info.inputs_read & VARYING_BIT_POS) ||
          BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_FRAG_COORD)) {
         ureg_property(c->ureg, TGSI_PROPERTY_FS_COORD_ORIGIN,
                       s->info.fs.origin_upper_left ? TGSI_FS_COORD_ORIGIN_UPPER_LEFT
                                                    : TGSI_FS_COORD_ORIGIN_LOWER_LEFT);

         ureg_property(c->ureg, TGSI_PROPERTY_FS_COORD_PIXEL_CENTER,
                       s->info.fs.pixel_center_integer ? TGSI_FS_COORD_PIXEL_CENTER_INTEGER
                                                       : TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER);
      }
   }
   /* Emit the main function */
   nir_function_impl *impl = nir_shader_get_entrypoint(c->s);
   ntr_emit_impl(c, impl);

   /* For FS, populate the FS-specific compiler outputs. */
   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      struct r300_fragment_program_compiler *fc =
         (struct r300_fragment_program_compiler *)compiler;
      rc.f->uses_discard = s->info.fs.uses_discard;
      fc->OutputDepth = c->fs_output_depth_index >= 0 ? c->fs_output_depth_index
                                                      : c->num_outputs;
      for (unsigned i = 0; i < ARRAY_SIZE(c->fs_output_color_index); i++) {
         fc->OutputColor[i] = c->fs_output_color_index[i] >= 0
                                ? c->fs_output_color_index[i]
                                : c->num_outputs;
      }
   } else if (s->info.stage == MESA_SHADER_VERTEX) {
      rc.v->num_inputs = s->num_inputs;
   }

   rc_calculate_inputs_outputs(compiler);

   ureg_destroy(c->ureg);

   ralloc_free(c);
   ralloc_free(s);
}
