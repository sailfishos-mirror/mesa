/*
 * Copyright © 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <string.h>
#include <xf86drm.h>

#include "util/macros.h"
#include "util/ralloc.h"
#include "util/timespec.h"

#include "pan_perf.h"

#include <drm-uapi/panfrost_drm.h>
#include <lib/kmod/pan_kmod.h>
#include <lib/pan_props.h>
#include <pan_perf_metrics.h>

uint32_t
pan_perf_counter_read(const struct pan_perf *perf,
                      const struct pan_perf_counter *counter, uint8_t blk_idx)
{
   unsigned offset = perf->category_offset[counter->category_id];
   offset += counter->offset;
   assert(offset < perf->n_counter_values);

   return perf->counter_values[offset];
}

uint32_t
pan_perf_counter_read_sum(const struct pan_perf *perf,
                          const struct pan_perf_counter *counter)
{
   /* If counter belongs to shader core, sum values for all cores. */
   uint8_t blk_cnt =
      mali_perf_block_count(counter->category_id, &perf->constants);
   uint32_t ret = 0;

   for (uint8_t blk_idx = 0; blk_idx < blk_cnt; blk_idx++)
      ret += pan_perf_counter_read(perf, counter, blk_idx);

   return ret;
}

static const struct pan_perf_config *
pan_lookup_counters(const char *name)
{
   for (unsigned i = 0; i < ARRAY_SIZE(pan_perf_configs); ++i) {
      if (strcmp(pan_perf_configs[i]->name, name) == 0)
         return pan_perf_configs[i];
   }

   return NULL;
}

void
pan_perf_destroy(struct pan_perf *perf)
{
   if (!perf)
      return;

   if (perf->dev)
      pan_kmod_dev_destroy(perf->dev);

   free(perf);
}

struct pan_perf *
pan_perf_create(int fd)
{
   ASSERTED drmVersionPtr version = drmGetVersion(fd);

   /* We only support panfrost at the moment. */
   if (!version) {
      mesa_loge("Not a DRM device");
      return NULL;
   }

   if (!strcmp(version->name, "panfrost")) {
      mesa_loge("Kerner driver not supported");
      drmFreeVersion(version);
      return NULL;
   }

   drmFreeVersion(version);

   struct pan_perf *perf = calloc(1, sizeof(*perf));
   if (!perf) {
      mesa_loge("Could not allocate pan_perf instance");
      return NULL;
   }

   perf->dev = pan_kmod_dev_create(fd, 0, NULL);
   if (!perf->dev) {
      mesa_loge("Could not create kmod device");
      goto err_destroy_perf;
   }

   struct pan_kmod_dev_props props = perf->dev->props;

   perf->constants.ext_bus_byte_size = pan_query_bus_width(&props);
   perf->constants.l2_cache_count = pan_query_l2_slices(&props);
   perf->constants.shader_core_count = pan_query_core_count(&props);

   const struct pan_model *model =
      pan_get_model(props.gpu_id, props.gpu_variant);
   if (model == NULL) {
      mesa_loge("GPU not supported");
      goto err_destroy_perf;
   }

   perf->cfg = pan_lookup_counters(model->performance_counters);
   if (perf->cfg == NULL) {
      mesa_loge("Performance counters missing!");
      goto err_destroy_perf;
   }

   // Generally counter blocks are laid out in the following order:
   // Job manager, tiler, one or more L2 caches, and one or more shader cores.
   unsigned l2_slices = pan_query_l2_slices(&props);
   perf->core_id_range = pan_query_core_id_range(&props);

   uint32_t n_blocks = 2 + l2_slices + perf->core_id_range;
   perf->n_counter_values = MALI_PERF_MAX_COUNTERS_PER_BLOCK * n_blocks;
   perf->counter_values = ralloc_array(perf, uint32_t, perf->n_counter_values);

   /* Setup the layout */
   perf->category_offset[MALI_PERF_BLOCK_GPU_FRONT_END] =
      MALI_PERF_MAX_COUNTERS_PER_BLOCK * 0;
   perf->category_offset[MALI_PERF_BLOCK_TILER] =
      MALI_PERF_MAX_COUNTERS_PER_BLOCK * 1;
   perf->category_offset[MALI_PERF_BLOCK_MEMSYS] =
      MALI_PERF_MAX_COUNTERS_PER_BLOCK * 2;
   perf->category_offset[MALI_PERF_BLOCK_SHADER_CORE] =
      MALI_PERF_MAX_COUNTERS_PER_BLOCK * (2 + l2_slices);

   return perf;

err_destroy_perf:
   pan_perf_destroy(perf);
   return NULL;
}

static int
pan_perf_query(struct pan_perf *perf, uint32_t enable)
{
   struct drm_panfrost_perfcnt_enable perfcnt_enable = {enable, 0};
   return pan_kmod_ioctl(perf->dev->fd, DRM_IOCTL_PANFROST_PERFCNT_ENABLE,
                         &perfcnt_enable);
}

int
pan_perf_enable(struct pan_perf *perf, UNUSED uint64_t sampling_period_ns)
{
   return pan_perf_query(perf, 1 /* enable */);
}

int
pan_perf_disable(struct pan_perf *perf)
{
   return pan_perf_query(perf, 0 /* disable */);
}

int
pan_perf_dump(struct pan_perf *perf)
{
   // Dump performance counter values to the memory buffer pointed to by
   // counter_values
   struct drm_panfrost_perfcnt_dump perfcnt_dump = {
      (uint64_t)(uintptr_t)perf->counter_values};
   int ret = pan_kmod_ioctl(perf->dev->fd, DRM_IOCTL_PANFROST_PERFCNT_DUMP,
                            &perfcnt_dump);

   if (!ret) {
      struct timespec tp;

      clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
      perf->dump_ts = timespec_to_nsec(&tp);
   }

   return ret;
}
