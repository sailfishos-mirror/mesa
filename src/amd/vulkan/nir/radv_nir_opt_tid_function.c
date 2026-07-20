/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"
#include "nir_constant_expressions.h"
#include "radv_nir.h"

/* This pass optimizes shuffles and boolean alu where the source can be
 * expressed as a function of tid (only subgroup_id,
 * invocation_id or constant as inputs).
 * Shuffles are replaced by specialized intrinsics, boolean alu by inverse_ballot.
 * The pass first computes the function of tid (fotid) mask, and then uses constant
 * folding to compute the source for each invocation.
 *
 * This pass assumes that local_invocation_index = subgroup_id * subgroup_size + subgroup_invocation_id.
 * That is not guaranteed by the VK spec, but it's how amd hardware works, if the GFX12 INTERLEAVE_BITS_X/Y
 * fields are not used. This is also the main reason why this pass is currently radv specific.
 */

#define NIR_MAX_SUBGROUP_SIZE     128
#define NIR_MAX_SUBGROUP_BITSET_WORDS BITSET_WORDS(NIR_MAX_SUBGROUP_SIZE)
#define FOTID_MAX_RECURSION_DEPTH 16 /* totally arbitrary */

static inline unsigned
src_get_fotid_mask(nir_src src)
{
   return nir_def_instr(src.ssa)->pass_flags;
}

static inline unsigned
alu_src_get_fotid_mask(nir_alu_instr *instr, unsigned idx)
{
   unsigned unswizzled = src_get_fotid_mask(instr->src[idx].src);
   unsigned result = 0;
   for (unsigned i = 0; i < nir_ssa_alu_instr_src_components(instr, idx); i++) {
      bool is_fotid = unswizzled & (1u << instr->src[idx].swizzle[i]);
      result |= is_fotid << i;
   }
   return result;
}

static void
update_fotid_alu(nir_builder *b, nir_alu_instr *instr, const radv_nir_opt_tid_function_options *options)
{
   const nir_op_info *info = &nir_op_infos[instr->op];

   unsigned res = BITFIELD_MASK(instr->def.num_components);
   for (unsigned i = 0; res != 0 && i < info->num_inputs; i++) {
      unsigned src_mask = alu_src_get_fotid_mask(instr, i);
      if (info->input_sizes[i] == 0)
         res &= src_mask;
      else if (src_mask != BITFIELD_MASK(info->input_sizes[i]))
         res = 0;
   }

   instr->instr.pass_flags = (uint8_t)res;
}

static void
update_fotid_intrinsic(nir_builder *b, nir_intrinsic_instr *instr, const radv_nir_opt_tid_function_options *options)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_subgroup_invocation: {
      instr->instr.pass_flags = 1;
      break;
   }
   case nir_intrinsic_load_local_invocation_id: {
      if (b->shader->info.workgroup_size_variable)
         break;

      /* subgroup_invocation_id <-> local_id mapping is not strictly defined by
       * the spec.  We assume linear dispatch, and with DERIVATIVE_GROUP_QUADS
       * linear dispatch of quads.
       */
      unsigned partial_size = 1;
      for (unsigned i = 0; i < 3; i++) {
         partial_size *= b->shader->info.workgroup_size[i];

         const bool quad_x = i == 0 && b->shader->info.derivative_group == DERIVATIVE_GROUP_QUADS;
         if (partial_size * (quad_x ? 2 : 1) == b->shader->info.max_subgroup_size)
            instr->instr.pass_flags = (uint8_t)BITFIELD_MASK(i + 1);
      }
      if (partial_size <= b->shader->info.max_subgroup_size)
         instr->instr.pass_flags = 0x7;
      break;
   }
   case nir_intrinsic_load_local_invocation_index: {
      assert(b->shader->info.derivative_group != DERIVATIVE_GROUP_QUADS);
      if (b->shader->info.workgroup_size_variable)
         break;
      unsigned workgroup_size =
         b->shader->info.workgroup_size[0] * b->shader->info.workgroup_size[1] * b->shader->info.workgroup_size[2];
      if (workgroup_size <= b->shader->info.max_subgroup_size)
         instr->instr.pass_flags = 0x1;
      break;
   }
   case nir_intrinsic_inverse_ballot: {
      if (src_get_fotid_mask(instr->src[0]) == BITFIELD_MASK(instr->src[0].ssa->num_components)) {
         instr->instr.pass_flags = 0x1;
      }
      break;
   }
   default: {
      break;
   }
   }
}

static void
update_fotid_load_const(nir_load_const_instr *instr)
{
   instr->instr.pass_flags = (uint8_t)BITFIELD_MASK(instr->def.num_components);
}

static bool
update_fotid_instr(nir_builder *b, nir_instr *instr, const radv_nir_opt_tid_function_options *options)
{
   /* Gather a mask of components that are functions of tid. */
   instr->pass_flags = 0;

   switch (instr->type) {
   case nir_instr_type_alu:
      update_fotid_alu(b, nir_instr_as_alu(instr), options);
      break;
   case nir_instr_type_intrinsic:
      update_fotid_intrinsic(b, nir_instr_as_intrinsic(instr), options);
      break;
   case nir_instr_type_load_const:
      update_fotid_load_const(nir_instr_as_load_const(instr));
      break;
   default:
      break;
   }

   return false;
}

static bool
constant_fold_scalar(nir_scalar s, unsigned invocation_id, nir_shader *shader, nir_const_value *dest, unsigned depth)
{
   if (depth > FOTID_MAX_RECURSION_DEPTH)
      return false;

   memset(dest, 0, sizeof(*dest));

   if (nir_scalar_is_alu(s)) {
      nir_alu_instr *alu = nir_def_as_alu(s.def);
      nir_const_value sources[NIR_ALU_MAX_INPUTS][NIR_MAX_VEC_COMPONENTS];
      const nir_op_info *op_info = &nir_op_infos[alu->op];

      unsigned bit_size = 0;
      if (!nir_alu_type_get_type_size(op_info->output_type))
         bit_size = alu->def.bit_size;

      for (unsigned i = 0; i < op_info->num_inputs; i++) {
         if (!bit_size && !nir_alu_type_get_type_size(op_info->input_types[i]))
            bit_size = alu->src[i].src.ssa->bit_size;

         unsigned offset = 0;
         unsigned num_comp = op_info->input_sizes[i];
         if (num_comp == 0) {
            num_comp = 1;
            offset = s.comp;
         }

         for (unsigned j = 0; j < num_comp; j++) {
            nir_scalar src_scalar = nir_get_scalar(alu->src[i].src.ssa, alu->src[i].swizzle[offset + j]);
            if (!constant_fold_scalar(src_scalar, invocation_id, shader, &sources[i][j], depth + 1))
               return false;
         }
      }

      if (!bit_size)
         bit_size = 32;

      unsigned exec_mode = shader->info.float_controls_execution_mode;

      nir_const_value *srcs[NIR_ALU_MAX_INPUTS];
      for (unsigned i = 0; i < op_info->num_inputs; ++i)
         srcs[i] = sources[i];
      nir_const_value dests[NIR_MAX_VEC_COMPONENTS];
      if (op_info->output_size == 0) {
         nir_eval_const_opcode(alu->op, dests, NULL, 1, bit_size, srcs, exec_mode);
         *dest = dests[0];
      } else {
         nir_eval_const_opcode(alu->op, dests, NULL, s.def->num_components, bit_size, srcs, exec_mode);
         *dest = dests[s.comp];
      }
      return true;
   } else if (nir_scalar_is_intrinsic(s)) {
      switch (nir_scalar_intrinsic_op(s)) {
      case nir_intrinsic_load_subgroup_invocation:
      case nir_intrinsic_load_local_invocation_index: {
         *dest = nir_const_value_for_uint(invocation_id, s.def->bit_size);
         return true;
      }
      case nir_intrinsic_load_local_invocation_id: {
         const unsigned size_x = shader->info.workgroup_size[0];
         const unsigned size_y = shader->info.workgroup_size[1];
         unsigned local_ids[3];

         if (shader->info.derivative_group == DERIVATIVE_GROUP_QUADS) {
            /* x = (invocation_id / 4 * 2 + invocation_id % 2) % block_width */
            const unsigned quad_x = invocation_id / 4 * 2;
            const unsigned quad_sub_x = invocation_id % 2;
            local_ids[0] = (quad_x + quad_sub_x) % size_x;

            /* y = (invocation_id / block_width / 2 * 2 + (invocation_id / 2) % 2) % block_height */
            const unsigned quad_y = invocation_id / size_x / 2 * 2;
            const unsigned quad_sub_y = (invocation_id / 2) % 2;
            local_ids[1] = (quad_y + quad_sub_y) % size_y;
         } else {
            const unsigned xy = invocation_id % (size_x * size_y);
            local_ids[0] = xy % size_x;
            local_ids[1] = xy / size_x;
         }

         local_ids[2] = invocation_id / (size_x * size_y);
         *dest = nir_const_value_for_uint(local_ids[s.comp], s.def->bit_size);
         return true;
      }
      case nir_intrinsic_inverse_ballot: {
         nir_def *src = nir_def_as_intrinsic(s.def)->src[0].ssa;
         unsigned comp = invocation_id / src->bit_size;
         unsigned bit = invocation_id % src->bit_size;
         if (!constant_fold_scalar(nir_get_scalar(src, comp), invocation_id, shader, dest, depth + 1))
            return false;
         uint64_t ballot = nir_const_value_as_uint(*dest, src->bit_size);
         *dest = nir_const_value_for_bool(ballot & (1ull << bit), 1);
         return true;
      }
      default:
         break;
      }
   } else if (nir_scalar_is_const(s)) {
      *dest = nir_scalar_as_const_value(s);
      return true;
   }

   UNREACHABLE("unhandled scalar type");
   return false;
}

static bool
bool_scalar_to_ballot(nir_scalar s, nir_shader *shader, BITSET_WORD *mask)
{
   assert(s.def->bit_size == 1);

   if ((nir_def_instr(s.def)->pass_flags & BITFIELD_BIT(s.comp)) == 0)
      return false;

   for (unsigned i = 0; i < shader->info.max_subgroup_size; i++) {
      nir_const_value value;
      if (!constant_fold_scalar(s, i, shader, &value, 0))
         return false;

      if (nir_const_value_as_bool(value, 1))
         BITSET_SET(mask, i);
      else
         BITSET_CLEAR(mask, i);
   }

   return true;
}

struct fotid_context {
   const radv_nir_opt_tid_function_options *options;
   uint8_t src_invoc[NIR_MAX_SUBGROUP_SIZE];
   BITSET_WORD reads_zero[NIR_MAX_SUBGROUP_BITSET_WORDS];
   nir_shader *shader;
};

static bool
gather_read_invocation_shuffle(nir_def *src, struct fotid_context *ctx)
{
   nir_scalar s = {src, 0};

   /* Recursive constant folding for each invocation */
   for (unsigned i = 0; i < ctx->shader->info.max_subgroup_size; i++) {
      nir_const_value value;
      if (!constant_fold_scalar(s, i, ctx->shader, &value, 0))
         return false;
      ctx->src_invoc[i] = MIN2(nir_const_value_as_uint(value, src->bit_size), UINT8_MAX);
   }

   return true;
}

static nir_alu_instr *
get_singluar_user_bcsel(nir_def *def, unsigned *src_idx)
{
   if (def->num_components != 1 || !list_is_singular(&def->uses))
      return NULL;

   nir_alu_instr *bcsel = NULL;
   nir_foreach_use_including_if_safe (src, def) {
      if (nir_src_is_if(src) || nir_src_use_instr(src)->type != nir_instr_type_alu)
         return NULL;
      bcsel = nir_instr_as_alu(nir_src_use_instr(src));
      if (bcsel->op != nir_op_bcsel || bcsel->def.num_components != 1)
         return NULL;
      *src_idx = list_entry(src, nir_alu_src, src) - bcsel->src;
      break;
   }
   assert(*src_idx < 3);

   if (*src_idx == 0)
      return NULL;
   return bcsel;
}

static bool
gather_invocation_uses(nir_alu_instr *bcsel, unsigned shuffle_idx, struct fotid_context *ctx)
{
   nir_scalar s = nir_get_scalar(bcsel->src[0].src.ssa, bcsel->src[0].swizzle[0]);

   BITSET_WORD selects_other[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};

   if (!bool_scalar_to_ballot(s, ctx->shader, selects_other))
      return false;

   if (shuffle_idx == 1)
      BITSET_NOT(selects_other);

   unsigned i = 0;
   BITSET_FOREACH_SET (i, selects_other, ctx->shader->info.max_subgroup_size) {
      /* If this invocation selects the other source,
       * so we can read an undefined result.
       */
      ctx->src_invoc[i] = UINT8_MAX;
   }

   if (nir_src_is_const(bcsel->src[3 - shuffle_idx].src) && nir_src_as_uint(bcsel->src[3 - shuffle_idx].src) == 0) {
      __bitset_copy(ctx->reads_zero, selects_other, BITSET_WORDS(ctx->shader->info.max_subgroup_size));
      return true;
   } else {
      return false;
   }
}

static nir_def *
try_opt_bitwise_mask(nir_builder *b, nir_def *def, struct fotid_context *ctx)
{
   unsigned one = NIR_MAX_SUBGROUP_SIZE - 1;
   unsigned zero = NIR_MAX_SUBGROUP_SIZE - 1;
   unsigned copy = NIR_MAX_SUBGROUP_SIZE - 1;
   unsigned invert = NIR_MAX_SUBGROUP_SIZE - 1;

   /* Because we prefer non-zero and_mask we need to special case
    * broadcasts. Otherwise we might end up not matching if only
    * a few invocations matter.
    */
   bool all_equal = true;
   int first_valid = -1;

   for (unsigned i = 0; i < ctx->shader->info.max_subgroup_size; i++) {
      unsigned read = ctx->src_invoc[i];
      if (read >= ctx->shader->info.max_subgroup_size)
         continue; /* undefined result */

      if (first_valid < 0)
         first_valid = i;
      else if (read != ctx->src_invoc[first_valid])
         all_equal = false;

      copy &= ~(read ^ i);
      invert &= read ^ i;
      one &= read;
      zero &= ~read;
   }

   /* We didn't find valid masks for at least one bit. */
   if ((copy | zero | one | invert) != NIR_MAX_SUBGROUP_SIZE - 1)
      return NULL;

   unsigned and_mask = copy | invert;
   unsigned xor_mask = (one | invert) & ~copy;

#if 0
   fprintf(stderr, "and %x, xor %x \n", and_mask, xor_mask);

   assert(false);
#endif

   if (all_equal && first_valid < 0) {
      return nir_undef(b, def->num_components, def->bit_size);
   } else if (and_mask == 0x7f && xor_mask == 0) {
      return def;
   } else if (ctx->options->use_shuffle_xor && and_mask == 0x7f) {
      return nir_shuffle_xor(b, def, nir_imm_int(b, xor_mask));
   } else if (ctx->options->use_masked_swizzle_amd && (and_mask & 0x60) == 0x60 && xor_mask <= 0x1f) {
      return nir_masked_swizzle_amd(b, def, (xor_mask << 10) | (and_mask & 0x1f), .fetch_inactive = true);
   } else if (all_equal) {
      /* Oddly enough, we do this last. This is because of there is a DPP pattern,
       * we should prefer it - after all, DPP can be fused into VALU, but not readlane.
       */
      return nir_read_invocation(b, def, nir_imm_int(b, ctx->src_invoc[first_valid]));
   }

   return NULL;
}

static nir_def *
try_opt_rotate(nir_builder *b, nir_def *def, struct fotid_context *ctx)
{
   for (unsigned csize = 4; csize <= ctx->shader->info.max_subgroup_size; csize *= 2) {
      unsigned cmask = csize - 1;

      unsigned delta = UINT_MAX;
      for (unsigned i = 0; i < ctx->shader->info.max_subgroup_size; i++) {
         if (ctx->src_invoc[i] >= ctx->shader->info.max_subgroup_size)
            continue;

         if (ctx->src_invoc[i] >= i)
            delta = ctx->src_invoc[i] - i;
         else
            delta = csize - i + ctx->src_invoc[i];
         break;
      }

      if (delta >= csize || delta == 0)
         continue;

      bool use_rotate = true;
      for (unsigned i = 0; use_rotate && i < ctx->shader->info.max_subgroup_size; i++) {
         if (ctx->src_invoc[i] >= ctx->shader->info.max_subgroup_size)
            continue;
         use_rotate &= (((i + delta) & cmask) + (i & ~cmask)) == ctx->src_invoc[i];
      }

      if (use_rotate)
         return nir_rotate(b, def, nir_imm_int(b, delta), .cluster_size = csize);
   }

   return NULL;
}

static nir_def *
try_opt_dpp16_shift(nir_builder *b, nir_def *def, struct fotid_context *ctx)
{
   int delta = INT_MAX;
   for (unsigned i = 0; i < ctx->shader->info.max_subgroup_size; i++) {
      if (ctx->src_invoc[i] >= ctx->shader->info.max_subgroup_size)
         continue;
      delta = ctx->src_invoc[i] - i;
      break;
   }

   if (delta < -15 || delta > 15 || delta == 0)
      return NULL;

   for (unsigned i = 0; i < ctx->shader->info.max_subgroup_size; i++) {
      int read = i + delta;
      bool out_of_bounds = (read & ~0xf) != (i & ~0xf);
      if (BITSET_TEST(ctx->reads_zero, i) && !out_of_bounds)
         return NULL;
      if (ctx->src_invoc[i] >= ctx->shader->info.max_subgroup_size)
         continue;
      if (read != ctx->src_invoc[i] || out_of_bounds)
         return NULL;
   }

   return nir_dpp16_shift_amd(b, def, .base = delta);
}

static bool
opt_fotid_shuffle(nir_builder *b, nir_intrinsic_instr *instr, void *_options)
{
   const radv_nir_opt_tid_function_options *options = _options;
   if (instr->intrinsic != nir_intrinsic_shuffle)
      return false;
   if (nir_src_is_const(instr->src[1]))
      return false; /* Leave obvious broadcasts alone */
   if (!nir_def_instr(instr->src[1].ssa)->pass_flags)
      return false;

   struct fotid_context ctx = {
      .options = options,
      .reads_zero = {0},
      .shader = b->shader,
   };

   memset(ctx.src_invoc, 0xff, sizeof(ctx.src_invoc));

   if (!gather_read_invocation_shuffle(instr->src[1].ssa, &ctx))
      return false;

   unsigned src_idx = 0;
   nir_alu_instr *bcsel = get_singluar_user_bcsel(&instr->def, &src_idx);

   /* Generalize src_invoc by taking into account which invocations
    * do not use the shuffle result because of bcsel.
    */
   bool can_remove_bcsel = false;
   if (bcsel)
      can_remove_bcsel = gather_invocation_uses(bcsel, src_idx, &ctx);

#if 0
   for (int i = 0; i < b->shader->info.max_subgroup_size; i++) {
      fprintf(stderr, "invocation %d reads %d\n", i, ctx.src_invoc[i]);
   }

   for (int i = 0; i < b->shader->info.max_subgroup_size; i++) {
      fprintf(stderr, "invocation %d zero %d\n", i, BITSET_TEST(ctx.reads_zero, i));
   }
#endif

   b->cursor = nir_after_instr(&instr->instr);

   nir_def *res = NULL;

   if (can_remove_bcsel && options->use_dpp16_shift_amd) {
      res = try_opt_dpp16_shift(b, instr->src[0].ssa, &ctx);

      if (res) {
         /* Rewrite the bcsel as a move, we are not allowed to remove the
          * bcsel because it could break the safe instruction iteration.
          */
         nir_alu_src_rewrite_scalar(&bcsel->src[3 - src_idx], nir_get_scalar(res, 0));
      }
   }

   if (!res)
      res = try_opt_bitwise_mask(b, instr->src[0].ssa, &ctx);
   if (!res && options->use_clustered_rotate)
      res = try_opt_rotate(b, instr->src[0].ssa, &ctx);

   if (res) {
      nir_def_replace(&instr->def, res);
      return true;
   } else {
      return false;
   }
}

static bool
opt_fotid_bool(nir_builder *b, nir_alu_instr *instr, const radv_nir_opt_tid_function_options *options)
{
   nir_scalar s = nir_get_scalar(&instr->def, 0);

   BITSET_WORD ballot_set[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};

   if (!bool_scalar_to_ballot(s, b->shader, ballot_set))
      return false;

   b->cursor = nir_after_instr(&instr->instr);

   nir_const_value ballot_comp[NIR_MAX_VEC_COMPONENTS];

   for (unsigned i = 0; i < options->hw_ballot_num_comp; i++) {
      unsigned bit_start = i * options->hw_ballot_bit_size;

      uint64_t imm = BITSET_EXTRACT64(ballot_set, bit_start, options->hw_ballot_bit_size);

      ballot_comp[i] = nir_const_value_for_uint(imm, options->hw_ballot_bit_size);
   }

   nir_def *ballot = nir_build_imm(b, options->hw_ballot_num_comp, options->hw_ballot_bit_size, ballot_comp);
   nir_def *res = nir_inverse_ballot(b, ballot);
   nir_def_instr(res)->pass_flags = 1;

   nir_def_replace(&instr->def, res);
   return true;
}

struct fotid_init_state {
   const radv_nir_opt_tid_function_options *options;
   bool has_shuffle;
};

static bool
init_fotid_mask(nir_builder *b, nir_instr *instr, void *params)
{
   struct fotid_init_state *state = params;
   update_fotid_instr(b, instr, state->options);

   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (!state->options->hw_ballot_bit_size || !state->options->hw_ballot_num_comp)
         return false;
      if (alu->def.bit_size != 1 || alu->def.num_components > 1 || !instr->pass_flags)
         return false;
      return opt_fotid_bool(b, alu, state->options);
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      state->has_shuffle |= intrin->intrinsic == nir_intrinsic_shuffle;
      return false;
   }
   default:
      return false;
   }
}

bool
radv_nir_opt_tid_function(nir_shader *shader, const radv_nir_opt_tid_function_options *options)
{
   bool progress = false;

   struct fotid_init_state state = {
      .options = options,
      .has_shuffle = false,
   };

   /* The pass is split in two steps because the shuffle optimization needs the function of tid mask
    * on instruction that come after the shuffle. The first set also optimizes booleans to inverse_ballot
    * to reduce work during the second step.
    */
   progress |= nir_shader_instructions_pass(shader, init_fotid_mask, nir_metadata_control_flow, &state);
   if (state.has_shuffle) {
      progress |= nir_shader_intrinsics_pass(shader, opt_fotid_shuffle, nir_metadata_control_flow, (void *)options);
   }

   return progress;
}
