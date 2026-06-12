/*
 * Copyright © 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <string.h>
#include <xf86drm.h>

#include "util/macros.h"
#include "util/ralloc.h"

#include "pan_perf.h"

#include <drm-uapi/panfrost_drm.h>
#include <lib/kmod/pan_kmod.h>
#include <lib/pan_props.h>
#include <pan_perf_metrics.h>

uint32_t
pan_perf_counter_read(const struct pan_perf_counter *counter,
                      const struct pan_perf *perf)
{
   unsigned offset = perf->category_offset[counter->category_id];
   offset += counter->offset;
   assert(offset < perf->n_counter_values);

   uint32_t ret = perf->counter_values[offset];

   // If counter belongs to shader core, accumulate values for all other cores
   if (counter->category_id == MALI_PERF_BLOCK_SHADER_CORE) {
      for (uint32_t core = 1; core < perf->core_id_range; ++core) {
         ret += perf->counter_values[offset + MALI_PERF_MAX_COUNTERS_PER_BLOCK * core];
      }
   }

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
pan_perf_init(struct pan_perf *perf, int fd)
{
   ASSERTED drmVersionPtr version = drmGetVersion(fd);

   /* We only support panfrost at the moment. */
   assert(version && !strcmp(version->name, "panfrost"));

   drmFreeVersion(version);

   perf->dev = pan_kmod_dev_create(fd, 0, NULL);
   assert(perf->dev);

   struct pan_kmod_dev_props props = perf->dev->props;

   perf->constants.ext_bus_byte_size = pan_query_bus_width(&props);
   perf->constants.l2_cache_count = pan_query_l2_slices(&props);
   perf->constants.shader_core_count = pan_query_core_count(&props);

   const struct pan_model *model =
      pan_get_model(props.gpu_id, props.gpu_variant);
   if (model == NULL)
      UNREACHABLE("Invalid GPU ID");

   perf->cfg = pan_lookup_counters(model->performance_counters);

   if (perf->cfg == NULL)
      UNREACHABLE("Performance counters missing!");

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
}

static int
pan_perf_query(struct pan_perf *perf, uint32_t enable)
{
   struct drm_panfrost_perfcnt_enable perfcnt_enable = {enable, 0};
   return pan_kmod_ioctl(perf->dev->fd, DRM_IOCTL_PANFROST_PERFCNT_ENABLE,
                         &perfcnt_enable);
}

int
pan_perf_enable(struct pan_perf *perf)
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
   return pan_kmod_ioctl(perf->dev->fd, DRM_IOCTL_PANFROST_PERFCNT_DUMP,
                         &perfcnt_dump);
}
