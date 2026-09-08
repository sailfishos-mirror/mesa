/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based on si_state.c
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_cs.h"
#include "tools/radv_debug.h"
#include "tools/radv_sqtt.h"
#include "radv_buffer.h"
#include "radv_sdma.h"
#include "radv_shader.h"
#include "sid.h"

#include "ac_barrier.h"

void
radv_cs_emit_write_event_eop(struct radv_cmd_stream *cs, enum amd_gfx_level gfx_level, unsigned event,
                             unsigned event_flags, unsigned dst_sel, unsigned int_sel, unsigned data_sel, uint64_t va,
                             uint32_t new_fence, uint64_t gfx9_eop_bug_va)
{
   assert(cs->hw_ip == AMD_IP_GFX || cs->hw_ip == AMD_IP_COMPUTE);

   /* The EOP bug is specific to GFX9. Though, RadeonSI also implements it for GFX6-8 but it
    * shouldn't be necessary because it's using SURFACE_SYNC to flush L2. See
    * waEventWriteEopPrematureL2Inv in PAL.
    */
   const uint64_t eop_bug_va = gfx_level >= GFX9 ? gfx9_eop_bug_va : va;

   ac_emit_cp_release_mem(cs->b, gfx_level, cs->hw_ip, event, event_flags, dst_sel, int_sel, data_sel, va, new_fence,
                          eop_bug_va);
}

static enum ac_barrier_flags
radv_to_ac_barrier_flags(enum amd_gfx_level gfx_level, enum radv_cmd_flush_bits flush_bits)
{
   enum ac_barrier_flags flags = 0;

   if (flush_bits & RADV_CMD_FLAG_INV_ICACHE)
      flags |= AC_BARRIER_INV_ICACHE;
   if (flush_bits & RADV_CMD_FLAG_INV_SCACHE)
      flags |= AC_BARRIER_INV_SMEM;
   if (flush_bits & RADV_CMD_FLAG_INV_VCACHE)
      flags |= AC_BARRIER_INV_VMEM;
   if (flush_bits & RADV_CMD_FLAG_INV_L2)
      flags |= AC_BARRIER_INV_L2;
   if (flush_bits & RADV_CMD_FLAG_WB_L2)
      flags |= AC_BARRIER_WB_L2;
   if (gfx_level < GFX12 && flush_bits & RADV_CMD_FLAG_INV_L2_METADATA)
      flags |= AC_BARRIER_INV_L2_METADATA;

   if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB)
      flags |= AC_BARRIER_SYNC_AND_INV_CB;
   if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB_META)
      flags |= AC_BARRIER_SYNC_AND_INV_CB_META;
   if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB)
      flags |= AC_BARRIER_SYNC_AND_INV_DB;
   if (gfx_level < GFX10 && flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB_META)
      flags |= AC_BARRIER_SYNC_AND_INV_DB_META;

   if (flush_bits & RADV_CMD_FLAG_VS_PARTIAL_FLUSH)
      flags |= AC_BARRIER_SYNC_VS;
   if (flush_bits & RADV_CMD_FLAG_PS_PARTIAL_FLUSH)
      flags |= AC_BARRIER_SYNC_PS;
   if (flush_bits & RADV_CMD_FLAG_CS_PARTIAL_FLUSH)
      flags |= AC_BARRIER_SYNC_CS;

   if (flush_bits & RADV_CMD_FLAG_VGT_FLUSH)
      flags |= AC_BARRIER_VGT_FLUSH;
   if (flush_bits & RADV_CMD_FLAG_VGT_STREAMOUT_SYNC)
      flags |= AC_BARRIER_VGT_STREAMOUT_SYNC;

   if (flush_bits & RADV_CMD_FLAG_START_PIPELINE_STATS)
      flags |= AC_BARRIER_PIPELINESTAT_START;
   if (flush_bits & RADV_CMD_FLAG_STOP_PIPELINE_STATS)
      flags |= AC_BARRIER_PIPELINESTAT_STOP;

   if (flush_bits & RADV_CMD_FLAG_PFP_SYNC_ME)
      flags |= AC_BARRIER_PFP_SYNC_ME;

   return flags;
}

static enum ac_pws_acquire_point
radv_to_ac_pws_acquire_point(enum radv_pws_acquire_point pws_acquire_point)
{
   switch (pws_acquire_point) {
   case RADV_PWS_ACQUIRE_POINT_NONE:
      return AC_PWS_ACQUIRE_POINT_NONE;
   case RADV_PWS_ACQUIRE_POINT_PRE_DEPTH:
      return AC_PWS_ACQUIRE_POINT_PRE_DEPTH;
   case RADV_PWS_ACQUIRE_POINT_ME:
      return AC_PWS_ACQUIRE_POINT_ME;
   case RADV_PWS_ACQUIRE_POINT_PFP:
      return AC_PWS_ACQUIRE_POINT_PFP;
   default:
      UNREACHABLE("Invalid RADV PWS acquire point");
   }
}

void
radv_cs_emit_cache_flush(struct radeon_winsys *ws, struct radv_cmd_stream *cs, enum amd_gfx_level gfx_level,
                         uint32_t *flush_cnt, uint64_t flush_va, enum radv_cmd_flush_bits flush_bits,
                         enum ac_rgp_flush_bits *rgp_flush_bits, enum radv_pws_acquire_point pws_acquire_point,
                         uint64_t gfx9_eop_bug_va)
{
   struct ac_barrier_state barrier = {
      .flags = radv_to_ac_barrier_flags(gfx_level, flush_bits),
      .pws_acquire_point = radv_to_ac_pws_acquire_point(pws_acquire_point),
      .wait_mem_va = flush_va,
      .wait_mem_number = flush_cnt,
      .eop_bug_va = gfx9_eop_bug_va,
   };

   radeon_check_space(ws, cs->b, 128);

   ac_emit_barrier(cs->b, gfx_level, cs->hw_ip, &barrier, NULL, rgp_flush_bits);
}

void
radv_init_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs, const enum amd_ip_type ip_type)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   cs->buffered_sh_regs.num = 0;
   cs->hw_ip = ip_type;

   ac_init_tracked_regs(&cs->tracked_regs, &pdev->info, false);
}

VkResult
radv_create_cmd_stream(const struct radv_device *device, const enum amd_ip_type ip_type, const bool is_secondary,
                       struct radv_cmd_stream **cs_out)
{
   struct radeon_winsys *ws = device->ws;
   struct radv_cmd_stream *cs;

   cs = malloc(sizeof(*cs));
   if (!cs)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   radv_init_cmd_stream(device, cs, ip_type);

   cs->b = ws->cs_create(ws, ip_type, is_secondary);
   if (!cs->b) {
      free(cs);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   *cs_out = cs;
   return VK_SUCCESS;
}

void
radv_reset_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   struct radeon_winsys *ws = device->ws;

   radv_init_cmd_stream(device, cs, cs->hw_ip);

   ws->cs_reset(cs->b);
}

VkResult
radv_finalize_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   struct radeon_winsys *ws = device->ws;

   return ws->cs_finalize(cs->b);
}

void
radv_destroy_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   struct radeon_winsys *ws = device->ws;

   ws->cs_destroy(cs->b);
   free(cs);
}
