/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler/gen/gen.h"
#include "dev/intel_debug.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "jay.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

static inline gen_reg_type
to_gen_reg_type(enum jay_type type)
{
   /* clang-format off */
   switch (type) {
   case JAY_TYPE_UNTYPED:
   case JAY_TYPE_U8:   return GEN_TYPE_UB;
   case JAY_TYPE_U16:  return GEN_TYPE_UW;
   case JAY_TYPE_U32:  return GEN_TYPE_UD;
   case JAY_TYPE_U64:  return GEN_TYPE_UQ;
   case JAY_TYPE_S8:   return GEN_TYPE_B;
   case JAY_TYPE_S16:  return GEN_TYPE_W;
   case JAY_TYPE_S32:  return GEN_TYPE_D;
   case JAY_TYPE_S64:  return GEN_TYPE_Q;
   case JAY_TYPE_F16:  return GEN_TYPE_HF;
   case JAY_TYPE_F32:  return GEN_TYPE_F;
   case JAY_TYPE_F64:  return GEN_TYPE_DF;
   case JAY_TYPE_BF16: return GEN_TYPE_BF;
   default: UNREACHABLE("invalid type");
   }
   /* clang-format on */
}

#define GEN_INST_BYTES 16

struct jay_codegen {
   const struct intel_device_info *devinfo;
   void *mem_ctx;

   gen_inst *insts;
   int num_insts;
   int insts_cap;

   /* Indices of the active loop headers. */
   struct util_dynarray loop_stack;

   uint8_t *output;
   int output_size;

   /* struct intel_shader_reloc */
   struct util_dynarray relocs;

   struct {
      uint8_t exec_size;
      uint8_t chan_offset;
      uint8_t flag_nr;
      uint8_t flag_subnr;
      gen_predicate pred_control;
      bool pred_inv;
      bool no_mask;
      bool saturate;
      gen_swsb swsb;
   } state;
};

static ATTRIBUTE_NOINLINE gen_inst *
jc_append(struct jay_codegen *jc, gen_opcode opcode)
{
   assert(jc->num_insts < jc->insts_cap);
   gen_inst *gen = &jc->insts[jc->num_insts++];
   *gen = (gen_inst) {
      .opcode = opcode,
      .exec_size = jc->state.exec_size,
      .chan_offset = jc->state.chan_offset,
      .flag_nr = jc->state.flag_nr,
      .flag_subnr = jc->state.flag_subnr,
      .pred_control = jc->state.pred_control,
      .pred_inv = jc->state.pred_inv,
      .no_mask = jc->state.no_mask,
      .saturate = jc->state.saturate,
      .swsb = jc->state.swsb,
   };
   return gen;
}

#define jc_append1(_opcode, _dst, _src0)                                       \
   ({                                                                          \
      gen_inst *_gen = jc_append(jc, (_opcode));                               \
      _gen->dst = (_dst);                                                      \
      _gen->src[0] = (_src0);                                                  \
      _gen;                                                                    \
   })

#define jc_append2(_opcode, _dst, _src0, _src1)                                \
   ({                                                                          \
      gen_inst *_gen = jc_append(jc, (_opcode));                               \
      _gen->dst = (_dst);                                                      \
      _gen->src[0] = (_src0);                                                  \
      _gen->src[1] = (_src1);                                                  \
      _gen;                                                                    \
   })

#define jc_append3(_opcode, _dst, _src0, _src1, _src2)                         \
   ({                                                                          \
      gen_inst *_gen = jc_append(jc, (_opcode));                               \
      _gen->dst = (_dst);                                                      \
      _gen->src[0] = (_src0);                                                  \
      _gen->src[1] = (_src1);                                                  \
      _gen->src[2] = (_src2);                                                  \
      _gen;                                                                    \
   })

static inline gen_inst *
jc_MOV(struct jay_codegen *jc, gen_operand dst, gen_operand src)
{
   return jc_append1(GEN_OP_MOV, dst, src);
}

static void
jc_MOV_reloc(struct jay_codegen *jc,
             gen_operand dst,
             uint32_t param,
             uint32_t base)
{
   util_dynarray_append(&jc->relocs,
                        ((struct intel_shader_reloc) {
                           .id = param,
                           .type = INTEL_SHADER_RELOC_TYPE_MOV_IMM,
                           .offset = GEN_INST_BYTES * jc->num_insts,
                           .delta = base,
                        }));

   jc_MOV(jc, dst, gen_imm_ud(GEN_UNCOMPACTABLE_PATCH_IMM));
}

static inline gen_operand
to_gen_operand(
   jay_function *f, const jay_inst *I, signed idx, unsigned simd_offs, bool hi)
{
   const struct intel_device_info *devinfo = f->shader->devinfo;
   bool is_dest = idx < 0;
   enum jay_type type = is_dest ? I->type : jay_src_type(I, idx);
   jay_def d = is_dest ? I->dst : I->src[idx];
   hi |= d.hi;

   gen_operand R;
   unsigned reg = d.reg, count = jay_num_values(d);
   unsigned offset_B = 0, grf = 0;
   assert(!hi || d.file == GPR);

   if (count && (d.file == GPR || d.file == UGPR)) {
      struct jay_register_block block =
         jay_lookup_block(&f->shader->partition, d.reg, d.file);

      grf = block.start_grf;
      reg -= block.start_gpr;

      assert(reg + count <= block.len_gpr && "must not cross partitions");
   }

   if (jay_is_imm(d)) {
      /* Immediates have size restrictions but can zero extend */
      if (jay_type_size_bits(type) == 64) {
         type = jay_type_resize(type, 32);
      } else if (I->op == JAY_OPCODE_BFN) {
         assert(jay_as_uint(d) <= UINT16_MAX);
         type = JAY_TYPE_U16;
      }

      R = gen_imm_ud(jay_as_uint(d));
   } else if (jay_is_null(d)) {
      R = gen_null();
   } else if (d.file == UGPR || d.file == UACCUM) {
      grf += (reg / jay_ugpr_per_grf(f->shader));
      offset_B = (reg % jay_ugpr_per_grf(f->shader)) * 4;

      if (d.file == UGPR) {
         R = gen_grf(grf, 0);
      } else {
         R = gen_accumulator(grf);
      }
      R = gen_retype(gen_restride(R, 0, 1, 0), GEN_TYPE_UD);

      /* Handle 3-src restrictions and vectorized uniform code. */
      if (is_dest || jay_num_values(d) >= 4) {
         R = gen_restride(R, 8, 8, 1);
      }

      /* Some operations have special restrictions on the destination stride,
       * but if we write a single UGPR the stride is ignored..  Specify
       * whatever stride is needed to satisfy the rules.
       */
      if (is_dest && I->num_srcs > 0) {
         /* BSpec 56640 "Special Restrictions" says:
          *
          *    "Conversion between HF and Integer must be DWord-aligned
          *     and strided by a DWord on the destination."
          */
         enum jay_type src0_type = jay_src_type(I, 0);
         if ((I->type == JAY_TYPE_F16 && !jay_type_is_any_float(src0_type)) ||
             (src0_type == JAY_TYPE_F16 && !jay_type_is_any_float(I->type))) {
            assert(jay_num_values(d) == 1 && "must not vectorize HF<->Int");
            R = gen_restride(R, 8, 2, 4);
         }

         /* Packed floats have restrictions on mixed sizes.  Use <2>. */
         if (I->type == JAY_TYPE_F16 && jay_type_size_bits(src0_type) != 16) {
            assert(jay_num_values(d) == 1 && "must not vectorize mixed float");
            R = gen_restride(R, 4, 2, 2);
         }
      }
   } else if (d.file == GPR || d.file == ACCUM) {
      enum jay_stride def_stride =
         d.file == GPR ? jay_def_stride(f->shader, d) : JAY_STRIDE_4;
      uint32_t type_bits = jay_type_size_bits(type);
      unsigned stride_bits = jay_stride_to_bits(def_stride);
      unsigned simd_width = jay_simd_width_physical(f->shader, I);

      if (def_stride == JAY_STRIDE_2) {
         /* Select between lo/hi halves of the GPR */
         grf += reg * jay_grf_per_gpr(f->shader);
         offset_B = hi ? 2 * f->shader->dispatch_width : 0;
      } else {
         /* Treat low bits as an offset in 2-byte words into the GRF */
         unsigned r = (reg * 2) + hi;
         unsigned mask = BITFIELD_MASK(stride_bits / 32);
         grf += ((r & ~mask) / 2) * jay_grf_per_gpr(f->shader);
         offset_B = (r & mask) * 2;
      }

      if (d.file == GPR) {
         R = gen_restride(gen_grf(grf, 0), 8, 8, 1);
      } else {
         R = gen_restride(gen_accumulator(grf / 2), 8, 8, 1);
      }

      R = gen_byte_offset(devinfo, R, simd_offs * simd_width * stride_bits / 8);

      if (stride_bits == (type_bits * 4)) {
         R = gen_restride(R, 8, 2, 4);
      } else if (stride_bits == (type_bits * 2)) {
         R = gen_restride(R, 4, 2, 2);
      } else {
         assert(stride_bits == type_bits);
      }

      /* Broadcast is equivalent to <8, 8, 1> for SIMD1 instructions. Use that
       * instead due to regioning restrictions.
       */
      if (simd_width == 1) {
         R = gen_restride(R, 0, 1, 0);
      }
   } else if (jay_is_flag(d)) {
      /* Explicit flags act like UGPRs. As sources they broadcast to all lanes,
       * so we may ignore the SIMD offset. As destinations, they are written by
       * SIMD1 instructions and are never SIMD split.
       */
      assert(simd_offs == 0 || idx >= 0);
      unsigned offs_B = d.reg * (f->shader->dispatch_width / 8);
      R = gen_flag(offs_B / 2);
   } else if (d.file == J_ADDRESS) {
      R = gen_address(d.reg);
   } else if (d.file == J_ARF) {
      R = gen_arf(jay_base_index(d), 0);
   } else {
      UNREACHABLE("unexpected file");
   }

   R.negate = d.negate;
   R.abs = d.abs;
   R.type = to_gen_reg_type(type);
   return gen_byte_offset(devinfo, R, offset_B);
}

#define SRC(i) to_gen_operand(f, I, i, simd_offs, false)

#define OP0(hw)                                                                \
   case JAY_OPCODE_##hw:                                                       \
      jc_append(jc, GEN_OP_##hw);                                              \
      break;

#define OP1(jay, hw)                                                           \
   case JAY_OPCODE_##jay:                                                      \
      jc_append1(GEN_OP_##hw, dst, SRC(0));                                    \
      break;

#define OP2(jay, hw)                                                           \
   case JAY_OPCODE_##jay:                                                      \
      jc_append2(GEN_OP_##hw, dst, SRC(0), SRC(1));                            \
      break;

#define OP3(jay, hw)                                                           \
   case JAY_OPCODE_##jay:                                                      \
      jc_append3(GEN_OP_##hw, dst, SRC(0), SRC(1), SRC(2));                    \
      break;

#define OP3_SWAP(jay, hw)                                                      \
   case JAY_OPCODE_##jay:                                                      \
      jc_append3(GEN_OP_##hw, dst, SRC(2), SRC(1), SRC(0));                    \
      break;

static gen_operand
quad_swizzle(const struct intel_device_info *devinfo,
             gen_operand r,
             const jay_inst *I)
{
   static const struct {
      gen_region region;
      unsigned element;
   } map[] = {
      [JAY_QUAD_SWIZZLE_XXXX] = { { 4, 4, 0 }, 0 },
      [JAY_QUAD_SWIZZLE_YYYY] = { { 4, 4, 0 }, 1 },
      [JAY_QUAD_SWIZZLE_ZZZZ] = { { 4, 4, 0 }, 2 },
      [JAY_QUAD_SWIZZLE_WWWW] = { { 4, 4, 0 }, 3 },
      [JAY_QUAD_SWIZZLE_XXZZ] = { { 2, 2, 0 }, 0 },
      [JAY_QUAD_SWIZZLE_YYWW] = { { 2, 2, 0 }, 1 },
      [JAY_QUAD_SWIZZLE_XYXY] = { { 0, 2, 1 }, 0 },
      [JAY_QUAD_SWIZZLE_ZWZW] = { { 0, 2, 1 }, 2 },
   };

   enum jay_quad_swizzle swizzle = jay_quad_swizzle_swizzle(I);
   assert(swizzle < ARRAY_SIZE(map));
   r.region = map[swizzle].region;
   return gen_element_offset(devinfo, r, map[swizzle].element);
}

/* Runs once per SIMD-split, so must not modify the instruction! */
static void
emit(struct jay_codegen *jc,
     jay_function *f,
     const jay_inst *I,
     unsigned simd_offs)
{
   ASSERTED unsigned nr_ins_before = jc->num_insts;
   unsigned exec_size = jay_simd_width_physical(f->shader, I);
   // jay_print_inst(stdout, (jay_inst *) I);

   /* Replicate the SWSB regdist for SIMD split instructions if needed */
   gen_swsb dep = simd_offs && !I->replicate_dep ? gen_swsb_null() : I->dep;

   /* We do not allow SBID dependencies on SIMD split instructions since
    * individual groups could get shot down. This would require more tracking
    * and is unclear whether it's beneficial.
    */
   assert(simd_offs == 0 || I->dep.mode == GEN_SBID_NULL);

   if (I->decrement_dep) {
      unsigned delta = simd_offs * jay_macro_length(I);
      assert(dep.regdist > delta);
      dep.regdist -= delta;
   }

   jc->state.exec_size = exec_size;
   jc->state.no_mask = jay_is_no_mask(I);
   jc->state.chan_offset = simd_offs * exec_size;
   jc->state.swsb = dep;
   jc->state.saturate = I->saturate;
   jc->state.flag_nr = 0;
   jc->state.flag_subnr = 0;

   /* Grab the hardware predicate, corresponding either to a logical predicate
    * or SEL's selector.
    */
   const jay_def *pred = I->predication ? jay_inst_get_predicate((void *) I) :
                         I->op == JAY_OPCODE_SEL ? &I->src[2] :
                                                   NULL;

   jc->state.pred_control = pred ? GEN_PREDICATE_NORMAL : GEN_PREDICATE_NONE;
   jc->state.pred_inv = pred && pred->negate;

   gen_condition cmod = I->conditional_mod;

   if (!jay_is_null(I->cond_flag)) {
      assert(!(pred && pred->reg != I->cond_flag.reg) && "must be tied");
      pred = &I->cond_flag;
   }

   if (pred) {
      unsigned reg = pred->reg * jay_phys_flag_per_virt(f->shader);
      jc->state.flag_nr = reg / 2;
      jc->state.flag_subnr = reg % 2;
   }

   if (I->op == JAY_OPCODE_MIN) {
      cmod = GEN_CONDITION_LT;
   } else if (I->op == JAY_OPCODE_MAX) {
      cmod = GEN_CONDITION_GE;
   }

   gen_operand dst = to_gen_operand(f, I, -1, simd_offs, false);

   switch (I->op) {
      OP0(ELSE)
      OP0(ENDIF)
      OP0(BREAK)
      OP1(MOV, MOV)
      OP1(MODIFIER, MOV)
      OP1(RNDD, RNDD)
      OP1(RNDZ, RNDZ)
      OP1(RNDE, RNDE)
      OP1(FRC, FRC)
      OP1(BFREV, BFREV)
      OP1(CBIT, CBIT)
      OP1(NOT, NOT)
      OP1(FBL, FBL)
      OP1(FBH, FBH)
      OP1(LZD, LZD)
      OP2(ROL, ROL)
      OP2(ROR, ROR)
      OP2(AVG, AVG)
      OP2(ADD, ADD)
      OP2(MUL, MUL)
      OP2(SEL, SEL)
      OP2(MIN, SEL)
      OP2(MAX, SEL)
      OP2(MUL_32X16, MUL)
      OP2(AND, AND)
      OP2(AND_U32_U16, AND)
      OP2(OR, OR)
      OP2(XOR, XOR)
      OP2(ASR, ASR)
      OP2(SHR, SHR)
      OP2(SHL, SHL)
      OP2(BFI1, BFI1)
      OP2(MAC, MAC)
      OP3(BFI2, BFI2)
      OP3(ADD3, ADD3)
      OP3(CSEL, CSEL)
      OP3(DP4A_UU, DP4A)
      OP3(DP4A_SS, DP4A)
      OP3(DP4A_SU, DP4A)
      OP3_SWAP(MAD, MAD)
      OP3_SWAP(BFE, BFE)

   case JAY_OPCODE_LOOP_ONCE: {
      /* TODO: Is there a better way to do this? */
      assert(util_dynarray_num_elements(&jc->loop_stack, int) > 0);
      int header_idx = util_dynarray_pop(&jc->loop_stack, int);
      jc_append(jc, GEN_OP_BREAK);
      gen_inst *last = jc_append(jc, GEN_OP_WHILE);
      last->src[0].file = GEN_IMM;
      last->src[0].type = GEN_TYPE_D;
      last->src[0].imm = GEN_INST_BYTES * (header_idx - (jc->num_insts - 1));
      break;
   }

   case JAY_OPCODE_WHILE: {
      assert(util_dynarray_num_elements(&jc->loop_stack, int) > 0);
      int header_idx = util_dynarray_pop(&jc->loop_stack, int);
      gen_inst *last = jc_append(jc, GEN_OP_WHILE);
      last->src[0].file = GEN_IMM;
      last->src[0].type = GEN_TYPE_D;
      last->src[0].imm = GEN_INST_BYTES * (header_idx - (jc->num_insts - 1));
      break;
   }

   case JAY_OPCODE_IF:
      jc_append(jc, GEN_OP_IF);
      break;

   case JAY_OPCODE_MATH: {
      gen_inst *last =
         jc_append2(GEN_OP_MATH, dst, SRC(0),
                    gen_retype(gen_null(), to_gen_reg_type(I->type)));
      last->math.func = (gen_math) jay_math_op(I);
      break;
   }

   case JAY_OPCODE_BFN: {
      gen_inst *last = jc_append3(GEN_OP_BFN, dst, SRC(0), SRC(1), SRC(2));
      last->boolean_func_ctrl = jay_bfn_ctrl(I);
      break;
   }

   case JAY_OPCODE_DESWIZZLE_ODD: {
      bool hi = simd_offs == 0 ? true : jay_deswizzle_odd_src2_hi(I);
      jc->state.chan_offset = 0;
      jc_MOV(jc, dst,
             gen_byte_offset(jc->devinfo,
                             to_gen_operand(f, I, simd_offs, 0, false),
                             hi ? 64 : 0));
      break;
   }

   case JAY_OPCODE_DESWIZZLE_EVEN:
      jc->state.exec_size = 16;
      jc_MOV(jc, gen_byte_offset(jc->devinfo, dst, 64),
             gen_byte_offset(jc->devinfo, SRC(0),
                             jay_deswizzle_even_src_hi(I) * 64));
      break;

   case JAY_OPCODE_CVT: {
      unsigned index = jay_cvt_index(I);
      bool force_hi = false;

      /* We will apply a suboffset for the specific subword being converted. In
       * the case where we have a subword (16-bit) stride, accesses to the upper
       * half will be instead to a discontiguous GRF so we have to fix up. This
       * affects u8->u32 conversions.
       */
      if (I->src[0].file == GPR) {
         unsigned type_size_B = jay_type_size_bits(jay_cvt_src_type(I)) / 8;
         unsigned index_B = index * type_size_B;
         unsigned stride_B =
            jay_stride_to_bits(jay_def_stride(f->shader, I->src[0])) / 8;

         if (index_B >= stride_B) {
            assert(stride_B == 2 && index_B <= 4 && !I->src[0].hi);
            force_hi = true;
            index = (index_B % stride_B) / type_size_B;
         }
      }

      gen_operand src = to_gen_operand(f, I, 0, simd_offs, force_hi);
      src = gen_element_offset(jc->devinfo, src, index);
      jc_MOV(jc, dst, src);
      break;
   }

   case JAY_OPCODE_SYNC: {
      gen_inst *sync = jc_append(jc, GEN_OP_SYNC);
      sync->dst = gen_null();
      sync->src[0] = gen_null();
      if (!jay_is_null(I->src[0])) {
         sync->src[0] = gen_restride(SRC(0), 0, 1, 0);
      }
      sync->sync.func = (gen_sync_func) jay_sync_op(I);
      break;
   }

   case JAY_OPCODE_CMP:
      jc_append2(GEN_OP_CMP, dst, SRC(0), SRC(1));
      break;

   case JAY_OPCODE_MOV_IMM64:
      jc_MOV(jc, dst, gen_imm_uq(jay_mov_imm64_imm(I)));
      break;

   case JAY_OPCODE_RELOC:
      jc_MOV_reloc(jc, dst, jay_reloc_param(I), jay_reloc_base(I));
      break;

   case JAY_OPCODE_QUAD_SWIZZLE:
      /* Quad swizzle can get split down to SIMD4 even on Xe2 where we don't
       * have NibCtrl.  Fortunately, it's NoMask so it doesn't matter.
       */
      jc->state.chan_offset = 0;
      jc_MOV(jc, dst, quad_swizzle(jc->devinfo, SRC(0), I));
      break;

   case JAY_OPCODE_BROADCAST_IMM: {
      gen_operand src = SRC(0);
      if (src.file != GEN_IMM) {
         src = gen_element_offset(jc->devinfo, src, jay_broadcast_imm_lane(I));
         src = gen_restride(src, 0, 1, 0);
      }
      jc_MOV(jc, dst, src);
      break;
   }

   case JAY_OPCODE_SEND: {
      gen_operand desc = SRC(0);
      gen_operand ex_desc = SRC(1);
      gen_operand payload0 = gen_retype(SRC(2), GEN_TYPE_UD);
      gen_operand payload1 = gen_retype(SRC(3), GEN_TYPE_UD);

      gen_inst *last =
         jc_append(jc, jay_send_check_tdr(I) ? GEN_OP_SENDC : GEN_OP_SEND);
      last->send.eot = jay_send_eot(I);
      last->send.sfid = (gen_sfid) jay_send_sfid(I);
      last->dst = dst;
      last->dst.type = GEN_TYPE_UD;
      last->src[0] = payload0;
      last->src[1] = payload1;

      if (desc.file == GEN_IMM)
         last->send.desc_imm = desc.imm;
      else
         last->send.desc_is_reg = true;

      if (ex_desc.file == GEN_IMM) {
         assert(jay_send_ex_desc_imm(I) == 0);
         last->send.ex_desc_imm = ex_desc.imm;
      } else {
         last->send.ex_desc_is_reg = true;
         last->send.ex_desc_subnr = ex_desc.subnr;
         if (jay_send_ex_desc_imm(I))
            last->send.ex_desc_imm_extra = jay_send_ex_desc_imm(I);
      }

      if (jay_send_bindless(I))
         last->send.ex_bso = true;

      if (jay_send_bindless(I) || last->send.sfid == GEN_SFID_UGM)
         last->send.src1_len = jay_send_ex_mlen(I) / reg_unit(jc->devinfo);

      break;
   }

   /* Gfx20+ has separate Render Target Array indices for each pair of subspans
    * in order to support multiple polygons, so we need to use a <1;8,0> region
    * in order to select the word for each channel.
    */
   case JAY_OPCODE_EXTRACT_LAYER:
      jc_append2(GEN_OP_AND, dst,
                 gen_restride(gen_retype(SRC(simd_offs), GEN_TYPE_UW), 1, 8, 0),
                 gen_imm_uw(0x7ff));
      break;

   case JAY_OPCODE_EXPAND_QUAD:
      jc_MOV(jc, dst, gen_restride(SRC(simd_offs), 1, 4, 0));
      break;

   case JAY_OPCODE_OFFSET_PACKED_PIXEL_COORDS:
      jc->state.exec_size = 32;
      jc->state.chan_offset = 0;
      jc_append2(GEN_OP_ADD, gen_retype(dst, GEN_TYPE_UW),
                 gen_retype(SRC(0), GEN_TYPE_UW), gen_imm_uv(0x11100100));
      break;

   case JAY_OPCODE_LANE_ID_8:
      jc_MOV(jc, dst, gen_imm_uv(0x76543210));
      break;

   case JAY_OPCODE_ZIP_UGPR16:
      jc_MOV(jc, dst, to_gen_operand(f, I, simd_offs, 0, false));
      break;

   case JAY_OPCODE_EXTRACT_BYTE_PER_8LANES: {
      gen_operand src =
         gen_restride(gen_retype(SRC(simd_offs), GEN_TYPE_UB), 1, 8, 0);
      jc_MOV(jc, dst, src);
      break;
   }

   case JAY_OPCODE_SHR_ODD_SUBSPANS_BY_4:
      jc_append2(GEN_OP_SHR, dst, SRC(0), gen_imm_uv(0x44440000));
      break;

   case JAY_OPCODE_MUL_32: {
      gen_operand acc = gen_accumulator(0);
      acc.type = to_gen_reg_type(I->type);
      gen_operand src1 = gen_subscript(jc->devinfo, SRC(1), GEN_TYPE_UW, 0);

      jc_append2(GEN_OP_MUL, acc, SRC(0), src1);
      jc->state.swsb = gen_swsb_null();
      jc_append2(jay_mul_32_high(I) ? GEN_OP_MACH : GEN_OP_MACL, dst, SRC(0),
                 SRC(1));
      break;
   }

   case JAY_OPCODE_SHUFFLE: {
      gen_operand a0 = gen_address(0);
      gen_operand idx = gen_subscript(jc->devinfo, SRC(1), GEN_TYPE_UW, 0);
      gen_operand indirect = gen_grf(0, 0);
      indirect.type = GEN_TYPE_UD;
      indirect.indirect = true;
      indirect.region.vstride = GEN_VSTRIDE_ONE_DIMENSIONAL;
      indirect.addr_imm = 0;

      assert(I->src[0].file == GPR && jay_num_values(I->src[0]) == 1);
      struct jay_register_block block =
         jay_lookup_block(&f->shader->partition, I->src[0].reg, GPR);

      unsigned offset_B =
         (block.start_grf * jc->devinfo->grf_size) +
         ((I->src[0].reg - block.start_gpr) * 4 * f->shader->dispatch_width);

      jc_append2(GEN_OP_ADD, a0, idx, gen_imm_uw(offset_B));
      jc_MOV(jc, dst, indirect);
      break;
   }

   default:
      jay_print_inst(stderr, (jay_inst *) I);
      UNREACHABLE("Unhandled opcode");
   }

   if (cmod != GEN_CONDITION_NONE) {
      assert(jc->num_insts > 0);
      gen_inst *last = &jc->insts[jc->num_insts - 1];
      last->cmod = cmod;
   }

   assert(jc->num_insts == (nr_ins_before + jay_macro_length(I)) &&
          "Jay instructions must map 1:n to GEN instructions");
}

struct jay_shader_bin *
jay_to_binary(jay_shader *s,
              void *const_data,
              size_t const_data_size,
              bool debug)
{
   struct jay_shader_bin *bin = rzalloc(s, struct jay_shader_bin);
   void *mem_ctx = ralloc_context(NULL);

   int total_gen_insts = 0;
   jay_foreach_function(s, f) {
      jay_foreach_block(f, block) {
         jay_foreach_inst_in_block(block, I) {
            total_gen_insts +=
               (1 << jay_simd_split(s, I)) * jay_macro_length(I);
         }
      }
   }

   const unsigned max_code_size = total_gen_insts * GEN_INST_BYTES;
   const unsigned output_capacity =
      const_data_size > 0 ? align(max_code_size, 32) + const_data_size :
                            max_code_size;

   struct jay_codegen jc = {
      .devinfo = s->devinfo,
      .mem_ctx = mem_ctx,
      .insts = rzalloc_array(mem_ctx, gen_inst, total_gen_insts),
      .insts_cap = total_gen_insts,
      .output = rzalloc_size(bin, output_capacity),
   };
   util_dynarray_init(&jc.loop_stack, mem_ctx);
   util_dynarray_init(&jc.relocs, bin);

   struct brw_stage_prog_data *prog_data = &s->prog_data->base;
   int code_size;

   /* TODO: Multifunction properly */
   jay_foreach_function(s, f) {
      jay_foreach_block(f, block) {
         if (block->loop_header) {
            util_dynarray_append(&jc.loop_stack, jc.num_insts);
         }

         jay_foreach_inst_in_block(block, I) {
            for (unsigned i = 0; i < (1 << jay_simd_split(s, I)); ++i) {
               emit(&jc, f, I, i);
            }
         }
      }
   }

   assert(util_dynarray_num_elements(&jc.loop_stack, int) == 0);

   /* TODO: Check if jay still needs this normalization. */
   for (int i = 0; i < jc.num_insts; i++) {
      gen_inst *gen = &jc.insts[i];

      if (gen->exec_size == 1) {
         if (gen->src[0].file != GEN_BAD_FILE && gen->src[0].region.width == 1)
            gen->src[0] = gen_restride(gen->src[0], 0, 1, 0);
         if (gen->src[1].file != GEN_BAD_FILE && gen->src[1].region.width == 1)
            gen->src[1] = gen_restride(gen->src[1], 0, 1, 0);
      }

      if ((gen->dst.file == GEN_GRF || gen->dst.file == GEN_ARF) &&
          gen->dst.region.hstride == 0)
         gen->dst.region.hstride = 1;
   }

   int final_halt_offset = -1 /* TODO */;
   gen_finish_structured_cf(jc.insts, jc.num_insts, final_halt_offset);

   const unsigned num_relocs =
      util_dynarray_num_elements(&jc.relocs, struct intel_shader_reloc);

   int *inst_offsets =
      num_relocs > 0 ? rzalloc_array(jc.mem_ctx, int, jc.num_insts) : NULL;

   gen_encode_params enc_params = {
      .devinfo = jc.devinfo,
      .compact_all = true,
#ifdef NDEBUG
      .skip_validation = true,
#endif
      .insts = jc.insts,
      .num_insts = jc.num_insts,
      .mem_ctx = bin,
      .raw_bytes = jc.output,
      .raw_bytes_size = max_code_size,
      .encoded_offsets = inst_offsets,
   };

   bool encoded = gen_encode(&enc_params);
   if (!encoded) {
      gen_print_params print_params = {
         .devinfo = jc.devinfo,
         .fp = stderr,
         .insts = jc.insts,
         .num_insts = jc.num_insts,
         .errors = enc_params.errors,
         .num_errors = enc_params.num_errors,
      };
      gen_print(&print_params);
      ralloc_free(jc.mem_ctx);
      UNREACHABLE("invalid assembly");
   }

   code_size = enc_params.raw_bytes_size;
   jc.output_size = code_size;

   /* Update reloc offsets to use the actual encoded offsets, which
    * will account for instruction compaction.
    */
   for (unsigned i = 0; i < num_relocs; i++) {
      struct intel_shader_reloc *reloc =
         util_dynarray_element(&jc.relocs, struct intel_shader_reloc, i);
      assert(reloc->type == INTEL_SHADER_RELOC_TYPE_MOV_IMM);
      assert(reloc->offset % GEN_INST_BYTES == 0);

      unsigned inst_idx = reloc->offset / GEN_INST_BYTES;
      assert(inst_idx < (unsigned) jc.num_insts);

      reloc->offset = inst_offsets[inst_idx];
   }

   if (debug || s->archiver) {
      gen_print_params print_params = {
         .devinfo = jc.devinfo,
         .insts = jc.insts,
         .num_insts = jc.num_insts,
         .raw_bytes = jc.output,
         .raw_bytes_size = code_size,
      };

      if (debug) {
         print_params.fp = stdout;
         gen_print(&print_params);
      }

      if (s->archiver) {
         const char *filename =
            ralloc_asprintf(s, "GEN%u/0", s->dispatch_width);
         print_params.fp = debug_archiver_start_file(s->archiver, filename);
         gen_print(&print_params);
         debug_archiver_finish_file(s->archiver);
      }
   }

   assert(prog_data->const_data_size == 0);
   if (const_data_size > 0) {
      unsigned offset = align(jc.output_size, 32);
      assert(offset + const_data_size <= output_capacity);
      memcpy(jc.output + offset, const_data, const_data_size);
      jc.output_size = offset + const_data_size;
      prog_data->const_data_size = const_data_size;
      prog_data->const_data_offset = offset;
   }

   prog_data->relocs = jc.relocs.data;
   prog_data->num_relocs = num_relocs;

   bin->kernel = (const uint32_t *) jc.output;
   bin->size = jc.output_size;

   ralloc_free(jc.mem_ctx);
   return bin;
}
