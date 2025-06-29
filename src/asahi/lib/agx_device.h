/*
 * Copyright 2021 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <xf86drm.h>
#include "drm-uapi/asahi_drm.h"
#include "util/ralloc.h"
#include "util/simple_mtx.h"
#include "util/sparse_array.h"
#include "util/timespec.h"
#include "util/u_printf.h"
#include "util/vma.h"
#include "agx_bo.h"
#include "agx_pack.h"
#include "decode.h"
#include "layout.h"
#include "libagx_dgc.h"

#include "vdrm.h"

#include "asahi_proto.h"

enum agx_dbg {
   AGX_DBG_TRACE = BITFIELD_BIT(0),
   AGX_DBG_BODUMP = BITFIELD_BIT(1),
   AGX_DBG_NO16 = BITFIELD_BIT(2),
   AGX_DBG_DIRTY = BITFIELD_BIT(3),
   AGX_DBG_PRECOMPILE = BITFIELD_BIT(4),
   AGX_DBG_PERF = BITFIELD_BIT(5),
   AGX_DBG_NOCOMPRESS = BITFIELD_BIT(6),
   AGX_DBG_NOCLUSTER = BITFIELD_BIT(7),
   AGX_DBG_SYNC = BITFIELD_BIT(8),
   AGX_DBG_STATS = BITFIELD_BIT(9),
   AGX_DBG_RESOURCE = BITFIELD_BIT(10),
   AGX_DBG_BATCH = BITFIELD_BIT(11),
   AGX_DBG_NOWC = BITFIELD_BIT(12),
   AGX_DBG_SYNCTVB = BITFIELD_BIT(13),
   AGX_DBG_SMALLTILE = BITFIELD_BIT(14),
   AGX_DBG_NOMSAA = BITFIELD_BIT(15),
   AGX_DBG_NOSHADOW = BITFIELD_BIT(16),
   AGX_DBG_BODUMPVERBOSE = BITFIELD_BIT(17),
   AGX_DBG_SCRATCH = BITFIELD_BIT(18),
   AGX_DBG_NOSOFT = BITFIELD_BIT(19),
   AGX_DBG_FEEDBACK = BITFIELD_BIT(20),
   AGX_DBG_1QUEUE = BITFIELD_BIT(21),
   AGX_DBG_NOMERGE = BITFIELD_BIT(22),
};

/* How many power-of-two levels in the BO cache do we want? 2^14 minimum chosen
 * as it is the page size that all allocations are rounded to
 */
#define MIN_BO_CACHE_BUCKET (14) /* 2^14 = 16KB */
#define MAX_BO_CACHE_BUCKET (22) /* 2^22 = 4MB */

/* Fencepost problem, hence the off-by-one */
#define NR_BO_CACHE_BUCKETS (MAX_BO_CACHE_BUCKET - MIN_BO_CACHE_BUCKET + 1)

/* Forward decl only, do not pull in all of NIR */
struct nir_shader;

#define BARRIER_RENDER  (1 << DRM_ASAHI_SUBQUEUE_RENDER)
#define BARRIER_COMPUTE (1 << DRM_ASAHI_SUBQUEUE_COMPUTE)

struct agx_submit_virt {
   uint32_t extres_count;
   struct asahi_ccmd_submit_res *extres;
};

typedef struct {
   struct agx_bo *(*bo_alloc)(struct agx_device *dev, size_t size, size_t align,
                              enum agx_bo_flags flags);
   int (*bo_bind)(struct agx_device *dev, struct drm_asahi_gem_bind_op *ops,
                  uint32_t count);
   void (*bo_mmap)(struct agx_device *dev, struct agx_bo *bo, void *fixed_addr);
   ssize_t (*get_params)(struct agx_device *dev, void *buf, size_t size);
   int (*submit)(struct agx_device *dev, struct drm_asahi_submit *submit,
                 struct agx_submit_virt *virt);
   int (*bo_bind_object)(struct agx_device *dev,
                         struct drm_asahi_gem_bind_object *bind);
   int (*bo_unbind_object)(struct agx_device *dev, uint32_t object_handle);
} agx_device_ops_t;

int agx_bo_bind(struct agx_device *dev, struct agx_bo *bo, uint64_t addr,
                size_t size_B, uint64_t offset_B, uint32_t flags);

int agx_bind_timestamps(struct agx_device *dev, struct agx_bo *bo,
                        uint32_t *handle);

struct agx_device {
   uint32_t debug;

   /* Precompiled libagx binary table */
   const uint32_t **libagx_programs;

   char name[64];
   struct drm_asahi_params_global params;
   bool is_virtio;
   agx_device_ops_t ops;

   /* vdrm device */
   struct vdrm_device *vdrm;
   uint32_t next_blob_id;

   /* Device handle */
   int fd;

   /* VM handle */
   uint32_t vm_id;

   /* Global queue handle */
   uint32_t queue_id;

   /* VMA heaps */
   simple_mtx_t vma_lock;
   uint64_t shader_base;
   struct util_vma_heap main_heap;
   struct util_vma_heap usc_heap;
   uint64_t guard_size;

   /* To emulate sparse-resident buffers, we map buffers in both the bottom half
    * and top half of the address space. sparse_ro_offset controls the
    * partitioning. This is a power-of-two that &'s zero in bottom (read-write)
    * buffers but non-zero in top (read-only) shadow mappings.
    *
    * In other words, given an address X, we can check if it is in the top half
    * if (X & sparse_ro_offset) != 0.
    *
    * Given a bottom half address X, we can get the top half address
    * equivalently as (X + sparse_ro_offset) or (X | sparse_ro_offset).
    */
   uint64_t sparse_ro_offset;

   struct agx_bo *zero_bo, *scratch_bo;

   struct renderonly *ro;

   pthread_mutex_t bo_map_lock;
   struct util_sparse_array bo_map;
   uint32_t max_handle;

   struct {
      simple_mtx_t lock;

      /* List containing all cached BOs sorted in LRU (Least Recently Used)
       * order so we can quickly evict BOs that are more than 1 second old.
       */
      struct list_head lru;

      /* The BO cache is a set of buckets with power-of-two sizes.  Each bucket
       * is a linked list of free panfrost_bo objects.
       */
      struct list_head buckets[NR_BO_CACHE_BUCKETS];

      /* Current size of the BO cache in bytes (sum of sizes of cached BOs) */
      size_t size;

      /* Number of hits/misses for the BO cache */
      uint64_t hits, misses;
   } bo_cache;

   struct agxdecode_ctx *agxdecode;

   /* Prepacked USC Sampler word to bind the txf sampler, used for
    * precompiled shaders on both drivers.
    */
   struct agx_usc_sampler_packed txf_sampler;

   /* Simplified device selection */
   enum agx_chip chip;

   struct {
      uint64_t num;
      uint64_t den;
   } user_timestamp_to_ns;

   struct u_printf_ctx printf;
};

/*
 * Determine if an address is in the read-only section. See the documentation
 * for sparse_ro_offset.
 */
static inline bool
agx_addr_is_ro(struct agx_device *dev, uint64_t addr)
{
   return (addr & dev->sparse_ro_offset);
}

/*
 * Convert a read-write address to its read-only shadow address. See the
 * documentation for sparse_ro_offset.
 */
static inline uint64_t
agx_rw_addr_to_ro(struct agx_device *dev, uint64_t addr)
{
   assert(!agx_addr_is_ro(dev, addr));
   return addr + dev->sparse_ro_offset;
}

static inline void *
agx_bo_map_placed(struct agx_bo *bo, void *fixed_addr)
{
   if (!bo->_map)
      bo->dev->ops.bo_mmap(bo->dev, bo, fixed_addr);

   return bo->_map;
}

static inline void *
agx_bo_map(struct agx_bo *bo)
{
   return agx_bo_map_placed(bo, NULL);
}

static inline bool
agx_has_soft_fault(struct agx_device *dev)
{
   return (dev->params.features & DRM_ASAHI_FEATURE_SOFT_FAULTS) &&
          !(dev->debug & AGX_DBG_NOSOFT);
}

static uint32_t
agx_usc_addr(struct agx_device *dev, uint64_t addr)
{
   assert(addr >= dev->shader_base);
   assert((addr - dev->shader_base) <= UINT32_MAX);

   return addr - dev->shader_base;
}

bool agx_open_device(void *memctx, struct agx_device *dev);

void agx_close_device(struct agx_device *dev);

static inline struct agx_bo *
agx_lookup_bo(struct agx_device *dev, uint32_t handle)
{
   return util_sparse_array_get(&dev->bo_map, handle);
}

uint32_t agx_create_command_queue(struct agx_device *dev,
                                  enum drm_asahi_priority priority);
int agx_destroy_command_queue(struct agx_device *dev, uint32_t queue_id);

int agx_import_sync_file(struct agx_device *dev, struct agx_bo *bo, int fd);
int agx_export_sync_file(struct agx_device *dev, struct agx_bo *bo);

void agx_debug_fault(struct agx_device *dev, uint64_t addr);

uint64_t agx_get_gpu_timestamp(struct agx_device *dev);

static inline uint64_t
agx_gpu_timestamp_to_ns(struct agx_device *dev, uint64_t gpu_timestamp)
{
   return (gpu_timestamp * dev->user_timestamp_to_ns.num) /
          dev->user_timestamp_to_ns.den;
}

void agx_get_device_uuid(const struct agx_device *dev, void *uuid);
void agx_get_driver_uuid(void *uuid);
unsigned agx_get_num_cores(const struct agx_device *dev);

struct agx_device_key agx_gather_device_key(struct agx_device *dev);

struct agx_va *agx_va_alloc(struct agx_device *dev, uint64_t size_B,
                            uint64_t align_B, enum agx_va_flags flags,
                            uint64_t fixed_va);
void agx_va_free(struct agx_device *dev, struct agx_va *va, bool unbind);

static inline struct drm_asahi_cmd_header
agx_cmd_header(bool compute, uint16_t barrier_vdm, uint16_t barrier_cdm)
{
   return (struct drm_asahi_cmd_header){
      .cmd_type = compute ? DRM_ASAHI_CMD_COMPUTE : DRM_ASAHI_CMD_RENDER,
      .size = compute ? sizeof(struct drm_asahi_cmd_compute)
                      : sizeof(struct drm_asahi_cmd_render),
      .vdm_barrier = barrier_vdm,
      .cdm_barrier = barrier_cdm,
   };
}
