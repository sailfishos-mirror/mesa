/*
 * Copyright © 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_PERF_H
#define PAN_PERF_H

#include <stdint.h>

#include "mali_perf.h"

#include "util/timespec.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct pan_kmod_dev;
struct pan_kmod_dev_props;
struct pan_model;
struct pan_perf_category;
struct pan_perf;

struct pan_perf_counter {
   const char *name;
   const char *desc;
   const char *symbol_name;
   enum mali_perf_counter_units units;
   // Offset of this counter's value within the category
   uint32_t offset;
   enum mali_perf_block_type category_id;
};

struct pan_perf_category {
   const char *name;

   struct pan_perf_counter counters[MALI_PERF_MAX_COUNTERS_PER_BLOCK];
   uint32_t n_counters;

   /* Offset of this category within the counters memory block */
   unsigned offset;
};

struct pan_perf_config {
   const char *name;

   struct pan_perf_category categories[MALI_PERF_BLOCK_TYPE_COUNT];
   uint32_t n_categories;
};

struct pan_perf {
   struct pan_kmod_dev *dev;
   unsigned core_id_range;
   struct mali_perf_constants constants;
   const struct pan_perf_config *cfg;

   // Memory where to dump counter values
   uint32_t *counter_values;
   uint32_t n_counter_values;

   /* Offsets of categories */
   unsigned category_offset[MALI_PERF_BLOCK_TYPE_COUNT];

   uint64_t dump_ts;
};

uint32_t pan_perf_counter_read(const struct pan_perf *perf,
                               const struct pan_perf_counter *counter,
                               uint8_t blk_idx);

uint32_t pan_perf_counter_read_sum(const struct pan_perf *perf,
                                   const struct pan_perf_counter *counter);

struct pan_perf *pan_perf_create(int fd);

void pan_perf_destroy(struct pan_perf *perf);

int pan_perf_enable(struct pan_perf *perf, uint64_t sampling_period_ns);

int pan_perf_disable(struct pan_perf *perf);

int pan_perf_dump(struct pan_perf *perf);

static inline clockid_t
pan_perf_gpu_clock_id(const struct pan_perf *perf)
{
   return CLOCK_MONOTONIC_RAW;
}

static inline uint64_t
pan_perf_get_gpu_timestamp(const struct pan_perf *perf)
{
   struct timespec tp;

   clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
   return timespec_to_nsec(&tp);
}

static inline uint64_t
pan_perf_get_dump_timestamp(const struct pan_perf *perf)
{
   return perf->dump_ts;
}

static inline uint64_t
pan_perf_get_min_sampling_period(const struct pan_perf *perf)
{
   return 1000000;
}

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // PAN_PERF_H
