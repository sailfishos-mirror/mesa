/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "jay_ir.h"
#include "jay_private.h"

/*
 * jay_partition_grf partitions the register file for the entire shader,
 * satisfying functional and performance rules. The partition is specified in a
 * convenient form within this file, as a flat array of jay_partition_builder
 * structs, which build_partition translates to the more complicated
 * jay_partition structs.
 *
 * All functions must share the same partition for correctness with non-uniform
 * function calls. For unlinked library functions, we must use the ABI
 * partition (TODO).
 */
struct jay_partition_builder {
   enum jay_file file;
   enum jay_stride stride;
   signed len_grf;
   enum jay_block_type type;
};

static void
build_partition(jay_shader *shader, struct jay_partition_builder *b, unsigned n)
{
   unsigned base_grf = 0, base_gpr[JAY_NUM_RA_FILES] = { 0 };
   struct jay_partition *p = &shader->partition;

   *p = (struct jay_partition) {
      .units_x16[UGPR] = jay_ugpr_per_grf(shader) * 16,
      .units_x16[GPR] = 16 / jay_grf_per_gpr(shader),
      .units_x16[MEM] = 16 / jay_grf_per_gpr(shader),
   };

   /* Drop empty blocks and merge the resulting neighbours. This avoids needless
    * partition boundaries that would unnecessarily constrain RA, while allowing
    * the caller the convenience of passing in holes sometimes.
    */
   signed j = -1;
   for (unsigned i = 0; i < n; ++i) {
      struct jay_partition_builder B = b[i];
      if (j >= 0 &&
          B.file == b[j].file &&
          B.stride == b[j].stride &&
          B.type == b[j].type) {

         b[j].len_grf += B.len_grf;
      } else if (B.len_grf) {
         b[++j] = B;
      }
   }

   /* Translate to jay_register_block */
   for (signed i = 0; i <= j; ++i) {
      enum jay_file file = b[i].file;
      unsigned len_gpr = (b[i].len_grf * p->units_x16[file]) / 16;
      bool grf = file < JAY_NUM_GRF_FILES;
      assert(p->nr_blocks[file] < JAY_PARTITION_BLOCKS);

      p->blocks[file][p->nr_blocks[file]++] = (struct jay_register_block) {
         .start_grf = grf ? base_grf : 0,
         .start_gpr = base_gpr[file],
         .len_gpr = (b[i].len_grf * p->units_x16[file]) / 16,
         .stride = b[i].stride,
         .type = b[i].type,
      };

      if (file < JAY_NUM_GRF_FILES) {
         base_grf += b[i].len_grf;
         base_gpr[file] += len_gpr;
      }
   }

   /* Validate the well formedness of the partition we built above */
   BITSET_DECLARE(regs, JAY_NUM_PHYS_GRF) = { 0 };

   for (enum jay_file file = 0; file < JAY_NUM_GRF_FILES; ++file) {
      for (unsigned b = 0; b < p->nr_blocks[file]; ++b) {
         struct jay_register_block B = p->blocks[file][b];
         unsigned len_grf = (B.len_gpr * 16) / p->units_x16[file];

         assert(len_grf > 0 && "no empty partitions");
         assert(B.start_grf + len_grf <= JAY_NUM_PHYS_GRF && "GRF file size");
         assert(!BITSET_TEST_COUNT(regs, B.start_grf, len_grf) && "uniqueness");

         /* This requirement avoids invalid constructions like g127<2> */
         if (file == GPR && B.stride == JAY_STRIDE_8) {
            assert(util_is_aligned(len_grf, 2 * jay_grf_per_gpr(shader)) &&
                   "must be a multiple of 2 GPRs");
         }

         BITSET_SET_COUNT(regs, B.start_grf, len_grf);
      }
   }

   assert(BITSET_COUNT(regs) == JAY_NUM_PHYS_GRF && "all GRFs mapped");
}

void
jay_partition_grf(jay_shader *shader)
{
   /* Calculate the maximum register demand across all functions in the shader.
    * We will use this to choose a good partition.
    */
   unsigned demand[JAY_NUM_GRF_FILES] = { 0 };

   jay_foreach_function(shader, f) {
      jay_compute_liveness(f);
      jay_calculate_register_demands(f);

      demand[GPR] = MAX2(demand[GPR], f->demand[GPR]);
      demand[UGPR] = MAX2(demand[UGPR], f->demand[UGPR]);
   }

   /* We must have enough register file space for the register payload, plus the
    * reserved UGPRs in the case we spill. That UGPR interferes with everything
    * we preload so it needs to be reserved specially here for the worst case.
    */
   jay_foreach_preload(jay_shader_get_entrypoint(shader), I) {
      unsigned end = jay_preload_reg(I) + jay_num_values(I->dst);
      unsigned extra = I->dst.file == UGPR ? shader->dispatch_width : 0;
      assert(I->dst.file < JAY_NUM_GRF_FILES);
      demand[I->dst.file] = MAX2(demand[I->dst.file], end + extra);
   }

   /* Determine a good GPR/UGPR split informed by the demand calculation */
   unsigned grf_per_gpr = jay_grf_per_gpr(shader);
   unsigned ugpr_per_grf = jay_ugpr_per_grf(shader);
   unsigned uniform_grfs = DIV_ROUND_UP(demand[UGPR], ugpr_per_grf);

   /* We must have enough for SIMD1 images (TODO: Check if this actually
    * applies. Or if we could eliminate this with smarter partitioning even.)
    */
   unsigned min_ugprs = 16;
   min_ugprs = MAX2(min_ugprs, 256);

   /* TODO: We could partition more cleverly */
   uniform_grfs = align(uniform_grfs, grf_per_gpr);
   uniform_grfs = CLAMP(uniform_grfs, DIV_ROUND_UP(min_ugprs, ugpr_per_grf),
                        128 - (32 * grf_per_gpr));
   unsigned nonuniform_grfs = JAY_NUM_PHYS_GRF - uniform_grfs;

   /* Check the split */
   assert((uniform_grfs * ugpr_per_grf) >= min_ugprs);
   assert(nonuniform_grfs >= 32 * grf_per_gpr);
   assert((uniform_grfs + nonuniform_grfs) == JAY_NUM_PHYS_GRF);

   /* Set the targets for the virtual register file accordingly */
   shader->num_regs[GPR] = nonuniform_grfs / grf_per_gpr;
   shader->num_regs[UGPR] = uniform_grfs * ugpr_per_grf;

   unsigned spill_reservation = 0, mem_slots = 0;

   /* Spilling requires reserving UGPRs for the lowered SENDs */
   if (demand[GPR] > jay_gpr_limit(shader)) {
      spill_reservation = shader->dispatch_width / ugpr_per_grf;

      /* This should be an acceptable upper limit since we assign memory tightly
       * thanks to the usual SSA allocator guarantees.
       */
      mem_slots = demand[GPR] * grf_per_gpr;
      shader->num_regs[MEM] = demand[GPR];
   }

   unsigned payload_4[2] = { 0, 0 }, payload_u[2] = { grf_per_gpr, 0 };
   unsigned eot_u = 0, eot_4 = 0;

   if (shader->stage == MESA_SHADER_VERTEX) {
      payload_4[0] = 1;
      payload_4[1] = shader->prog_data->vue.urb_read_length * 8;
      payload_u[1] = shader->push_grfs;
      eot_4 = 16;
   } else if (shader->stage == MESA_SHADER_FRAGMENT) {
      /* The SIMD32 fragment payload splits GPRs into low and high GRFs, with
       * UGPRs mixed in between. jay_insert_payload_swizzle deals with this and
       * swizzles things appropriately, we just need the partition to have two
       * separate GPR block with a UGPR block in between. That requires the
       * number of GPRs in the payload to be even.
       */
      assert(util_is_aligned(shader->payload_gprs, grf_per_gpr) &&
             "payload constraint");

      payload_4[0] = shader->payload_gprs;
      payload_u[1] = (shader->payload_ugprs / ugpr_per_grf) - payload_u[0];
      payload_4[1] = grf_per_gpr == 2 ? shader->payload_gprs : 0;
      eot_4 = 14;
      eot_u = 1;
   } else {
      eot_u = 1;
   }

   /* EOT blocks are only relevant for platforms with Early EOT, otherwise any
    * register works fine.
    */
   if (!jay_has_early_eot(shader)) {
      eot_4 = eot_u = 0;
   }

   unsigned special_u = payload_u[0] + payload_u[1] + spill_reservation + eot_u;
   unsigned special_4 = payload_4[0] + payload_4[1] + eot_4;

   /* TODO: Make the stride partition smarter */
   unsigned grf_8 = 8 * grf_per_gpr;
   unsigned grf_2 = 8;

   struct jay_partition_builder blocks[] = {
      /* Stage-specific payload */
      { UGPR, 0, payload_u[0] },
      { GPR, JAY_STRIDE_4, payload_4[0] },
      { UGPR, 0, payload_u[1] },
      { GPR, JAY_STRIDE_4, payload_4[1] },

      /* General registers */
      { UGPR, 0, uniform_grfs - special_u },
      { GPR, JAY_STRIDE_4, nonuniform_grfs - (special_4 + grf_8 + grf_2) },
      { GPR, JAY_STRIDE_8, grf_8 },
      { GPR, JAY_STRIDE_2, grf_2 },

      /* Spilling registers */
      { UGPR, 0, spill_reservation, JAY_BLOCK_SPILL },
      { MEM, JAY_STRIDE_4, mem_slots },

      /* EOT */
      { UGPR, 0, eot_u, JAY_BLOCK_EOT },
      { GPR, JAY_STRIDE_4, eot_4, JAY_BLOCK_EOT },
   };

   build_partition(shader, blocks, ARRAY_SIZE(blocks));

   /* By construction of our partition, the entire GRF is used. */
   shader->prog_data->base.grf_used = JAY_NUM_PHYS_GRF;
}

#define ANSI_END    "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_ITALIC "\033[3m"

void
jay_print_partition(struct jay_partition *p)
{
   jay_foreach_ra_file(file) {
      if (p->nr_blocks[file]) {
         const char *files[JAY_NUM_RA_FILES] = { "GPR", "UGPR", "MEM" };
         printf("%s" ANSI_BOLD "    GRF      %s%s" ANSI_END "\n",
                file ? "\n" : "", files[file], file == GPR ? "    Stride" : "");
      }

      for (unsigned b = 0; b < p->nr_blocks[file]; ++b) {
         struct jay_register_block B = p->blocks[file][b];
         unsigned len_grf = (B.len_gpr * 16) / p->units_x16[file];

         printf("  %3u…%-3u  %3u…%-3u", B.start_grf, B.start_grf + len_grf - 1,
                B.start_gpr, B.start_gpr + B.len_gpr - 1);

         if (file == GPR) {
            printf("  %u-bit", jay_stride_to_bits(B.stride));
         }

         const char *types[JAY_BLOCK_TYPES] = { "", " EOT", " Spill" };
         printf(ANSI_ITALIC "%s" ANSI_END "\n", types[B.type]);
      }
   }

   printf("\n");
}
