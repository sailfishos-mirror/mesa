/*
 * Copyright (C) 2021-2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"

/* This optimization pass, intended to run once right before code emission,
 * analyzes direct word-aligned UBO reads and promotes a subset to moves to
 * push constant loads. It is the sole populator of the UBO push data
 * structure returned back to the command stream.
 */

/* Represents use data for a single UBO */

#define MAX_UBO_WORDS (65536 / 16)

struct pushable_ubo {
   BITSET_DECLARE(read, MAX_UBO_WORDS);
   BITSET_DECLARE(pushed, MAX_UBO_WORDS);
   uint8_t range[MAX_UBO_WORDS];
};

struct opt_push_ubo_ctx {
   struct pan_fau_layout *fau;

   /* Mask of UBOs which may be pushed */
   uint32_t pushable_ubos;

   /* Mask of UBOs that are still UBOs at the end of this pass */
   uint32_t ubo_mask;

   /* Per block analysis */
   unsigned nr_ubos;
   struct pushable_ubo *ubos;
};

struct ubo_range {
   int8_t ubo_idx;
   int8_t nr_words;
   uint16_t word;
};

static struct ubo_range
get_pushable_ubo_range(nir_intrinsic_instr *load,
                       const struct opt_push_ubo_ctx *ctx)
{
   /* We can't have any load_push_constant yet */
   assert(load->intrinsic != nir_intrinsic_load_push_constant);

   struct ubo_range range = {
      .ubo_idx = -1,
      .nr_words = -1,
   };

   if (load->intrinsic != nir_intrinsic_load_ubo ||
       !nir_src_is_const(load->src[0]) ||
       !nir_src_is_const(load->src[1]))
      return range;

   const uint32_t handle = nir_src_as_uint(load->src[0]);
   range.ubo_idx = pan_res_handle_get_index(handle);
   if (!(ctx->pushable_ubos & BITFIELD_BIT(range.ubo_idx)))
      return range;

   const uint32_t offset = nir_src_as_uint(load->src[1]);
   assert(load->def.bit_size >= 8);
   const uint8_t bytes = load->def.num_components * (load->def.bit_size / 8);

   /* We can't handle unaligned push constant access today */
   if ((offset % 4) != 0)
      return range;

   range.word = offset / 4;
   range.nr_words = DIV_ROUND_UP(bytes, 4);

   return range;
}

static bool
analyze_ubo_intr(nir_builder *b, nir_intrinsic_instr *load, void *data)
{
   struct opt_push_ubo_ctx *ctx = data;
   const struct ubo_range range = get_pushable_ubo_range(load, ctx);
   if (range.ubo_idx < 0 || range.nr_words < 0)
      return false;

   /* Blend constants are handled by bi_pick_blend_constants, don't
    * push them a second time.
    */
   if (b->shader->info.stage == MESA_SHADER_FRAGMENT) {
      /* PAN_UBO_SYSVALS from the gallium driver */
      unsigned sysval_ubo = 1;
      if (range.ubo_idx == sysval_ubo &&
          range.word + range.nr_words <= 4)
         return false;
   }

   struct pushable_ubo *ubo = &ctx->ubos[range.ubo_idx];
   BITSET_SET_COUNT(ubo->read, range.word, range.nr_words);
   ubo->range[range.word] = MAX2(ubo->range[range.word], range.nr_words);

   return false;
}

static void
add_ubo_push(struct opt_push_ubo_ctx *ctx, unsigned ubo_idx, unsigned word)
{
   struct pushable_ubo *ubo = &ctx->ubos[ubo_idx];
   assert(!BITSET_TEST(ubo->pushed, word));

   BITSET_SET(ubo->pushed, word);

   pan_fau_emit_reloc(ctx->fau, (struct pan_ubo_relocation) {
      .ubo = ubo_idx,
      .offset = word * 4,
   });
}

/* We always map blend constants from the first slot in the sysval UBO to the
 * first four FAU words, so that they can be accessed from a consistent
 * location from the blend shader.
 */
static void
add_blend_constants(struct opt_push_ubo_ctx *ctx)
{
   /* PAN_UBO_SYSVALS from the gallium driver */
   unsigned sysval_ubo = 1;
   assert(ctx->pushable_ubos & BITFIELD_BIT(sysval_ubo));

   /* Blend constants are the first, non-reorderable ("fixed") relocations */
   assert(ctx->fau->count == 0 && ctx->fau->reserved == 0);

   for (unsigned channel = 0; channel < 4; channel++)
      add_ubo_push(ctx, sysval_ubo, channel);
}

/* Select UBO words to push. A sophisticated implementation would consider the
 * number of uses and perhaps the control flow to estimate benefit. This is not
 * sophisticated. Select from the last UBO first to prioritize sysvals.
 */
static void
pick_ubo_push_words(struct opt_push_ubo_ctx *ctx)
{
   for (int ubo_idx = ctx->nr_ubos - 1; ubo_idx >= 0; --ubo_idx) {
      struct pushable_ubo *ubo = &ctx->ubos[ubo_idx];

      uint32_t word;
      BITSET_FOREACH_SET(word, ubo->read, MAX_UBO_WORDS) {
         /* Don't double-push */
         if (BITSET_TEST(ubo->pushed, word))
            continue;

         unsigned range = ubo->range[word];
         if (pan_fau_available(ctx->fau) < range)
            return;

         for (unsigned w = 0; w < range; w++)
            add_ubo_push(ctx, ubo_idx, word + w);
      }
   }
}

static bool
lower_ubo_intr(nir_builder *b, nir_intrinsic_instr *load, void *data)
{
   struct opt_push_ubo_ctx *ctx = data;
   const struct ubo_range range = get_pushable_ubo_range(load, ctx);
   if (range.ubo_idx < 0) {
      /* We don't even know the UBO index */
      ctx->ubo_mask = ~0;
      return false;
   }

   struct pushable_ubo *ubo = &ctx->ubos[range.ubo_idx];
   if (range.nr_words < 0) {
      /* We couldn't push this one */
      ctx->ubo_mask |= BITFIELD_BIT(range.ubo_idx);
      return false;
   }

   /* Check to see if we've pushed the whole range */
   for (unsigned w = 0; w < range.nr_words; w++) {
      if (!BITSET_TEST(ubo->pushed, range.word)) {
         ctx->ubo_mask |= BITFIELD_BIT(range.ubo_idx);
         return false;
      }
   }

   uint32_t fau_word =
      pan_lookup_pushed_ubo(ctx->fau, range.ubo_idx, range.word * 4);

   const uint16_t align_mul = nir_intrinsic_align_mul(load);
   const uint16_t align_offset = nir_intrinsic_align_offset(load);

   b->cursor = nir_before_instr(&load->instr);
   nir_def *val = nir_load_push_constant(b, load->def.num_components,
                                            load->def.bit_size,
                                            nir_imm_int(b, fau_word * 4),
                                            .align_mul = align_mul,
                                            .align_offset = align_offset);
   nir_def_replace(&load->def, val);

   return true;
}

bool
pan_nir_opt_push_ubo(nir_shader *nir,
                     uint32_t pushable_ubos,
                     struct pan_fau_layout *fau,
                     uint32_t *ubo_mask_out)
{
   struct opt_push_ubo_ctx ctx = {
      .fau = fau,
      .pushable_ubos = pushable_ubos,
      .nr_ubos = nir->info.num_ubos + 1,
   };

   ctx.ubos = rzalloc_array(nir, struct pushable_ubo, ctx.nr_ubos);

   /* Analyze load_ubo intrinsics */
   nir_shader_intrinsics_pass(nir, analyze_ubo_intr, nir_metadata_all, &ctx);

   /* We first pick the blend constants, those cannot be reordered */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      add_blend_constants(&ctx);

   pick_ubo_push_words(&ctx);

   bool progress = nir_shader_intrinsics_pass(nir, lower_ubo_intr,
                                              nir_metadata_control_flow, &ctx);

   *ubo_mask_out = ctx.ubo_mask;
   ralloc_free(ctx.ubos);

   return progress;
}
