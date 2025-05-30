/*
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AMDGPU_CS_H
#define AMDGPU_CS_H

#include "amdgpu_bo.h"
#include "util/u_memory.h"
#include "ac_linux_drm.h"
#include "drm-uapi/amdgpu_drm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Smaller submits means the GPU gets busy sooner and there is less
 * waiting for buffers and fences. Proof:
 *   http://www.phoronix.com/scan.php?page=article&item=mesa-111-si&num=1
 */
#define IB_MAX_SUBMIT_BYTES (80 * 1024)

struct amdgpu_ctx {
   struct pipe_reference reference;
   uint32_t ctx_handle;
   struct amdgpu_winsys *aws;
   ac_drm_bo user_fence_bo;
   uint32_t user_fence_bo_kms_handle;
   uint64_t *user_fence_cpu_address_base;

   /* Lost context status due to ioctl and allocation failures. */
   enum pipe_reset_status sw_status;
   unsigned flags;
};

struct amdgpu_cs_buffer {
   struct amdgpu_winsys_bo *bo;
   unsigned usage;
};

enum ib_type {
   IB_PREAMBLE,
   IB_MAIN,
   IB_NUM,
};

struct amdgpu_ib {
   /* A buffer out of which new IBs are allocated. */
   struct pb_buffer_lean   *big_buffer;
   uint8_t                 *big_buffer_cpu_ptr;
   uint64_t                gpu_address;
   unsigned                used_ib_space;

   /* The maximum seen size from cs_check_space. If the driver does
    * cs_check_space and flush, the newly allocated IB should have at least
    * this size.
    */
   unsigned                max_check_space_size;

   unsigned                max_ib_bytes;
   /* ptr_ib_size initially points to cs->csc->chunk_ib->ib_bytes.
    * If in amdgpu_cs_check_space() ib chaining is required, then ptr_ib_size will point
    * to indirect buffer packet size field.
    */
   uint32_t                *ptr_ib_size;
   bool                    is_chained_ib;
};

struct amdgpu_fence_list {
   struct pipe_fence_handle    **list;
   unsigned                    num;
   unsigned                    max;
};

struct amdgpu_buffer_list {
   unsigned                    max_buffers;
   unsigned                    num_buffers;
   struct amdgpu_cs_buffer     *buffers;
};

struct amdgpu_cs_context {
   struct drm_amdgpu_cs_chunk_ib chunk_ib[IB_NUM];
   uint32_t                    *ib_main_addr; /* the beginning of IB before chaining */

   struct amdgpu_winsys *aws;

   /* Buffers. */
   struct amdgpu_buffer_list   buffer_lists[NUM_BO_LIST_TYPES];
   int16_t                     *buffer_indices_hashlist;

   struct amdgpu_winsys_bo     *last_added_bo;
   unsigned                    last_added_bo_usage;

   struct amdgpu_seq_no_fences seq_no_dependencies;

   struct amdgpu_fence_list    syncobj_dependencies;
   struct amdgpu_fence_list    syncobj_to_signal;

   struct pipe_fence_handle    *fence;

   /* the error returned from cs_flush for non-async submissions */
   int                         error_code;

   /* TMZ: will this command be submitted using the TMZ flag */
   bool secure;
};

/* This high limit is needed for viewperf2020/catia. */
#define BUFFER_HASHLIST_SIZE 32768

struct amdgpu_cs {
   struct amdgpu_ib main_ib; /* must be first because this is inherited */
   struct amdgpu_winsys *aws;
   struct amdgpu_ctx *ctx;

   /*
    * Ensure a 64-bit alignment for drm_amdgpu_cs_chunk_fence.
    */
   struct drm_amdgpu_cs_chunk_fence fence_chunk;
   enum amd_ip_type ip_type;
   enum amdgpu_queue_index queue_index;

   /* Whether this queue uses amdgpu_winsys_bo::alt_fence instead of generating its own
    * sequence numbers for synchronization.
    */
   bool uses_alt_fence;

   /* Max AMDGPU_FENCE_RING_SIZE jobs can be submitted. Commands are being filled and submitted
    * between the two csc till AMDGPU_FENCE_RING_SIZE jobs are in queue. current_csc_index will
    * point to csc that will be filled by commands.
    */
   struct amdgpu_cs_context csc[2];
   int current_csc_index;
   /* buffer_indices_hashlist[hash(bo)] returns -1 if the bo
    * isn't part of any buffer lists or the index where the bo could be found.
    * Since 1) hash collisions of 2 different bo can happen and 2) we use a
    * single hashlist for the 3 buffer list, this is only a hint.
    * amdgpu_lookup_buffer uses this hint to speed up buffers look up.
    */
   int16_t buffer_indices_hashlist[BUFFER_HASHLIST_SIZE];

   /* Flush CS. */
   void (*flush_cs)(void *ctx, unsigned flags, struct pipe_fence_handle **fence);
   void *flush_data;
   bool noop;
   bool has_chaining;

   struct util_queue_fence flush_completed;
   struct pipe_fence_handle *next_fence;
   struct pb_buffer_lean *preamble_ib_bo;

   struct drm_amdgpu_cs_chunk_cp_gfx_shadow mcbp_fw_shadow_chunk;
};

struct amdgpu_fence {
   struct pipe_reference reference;
   uint32_t syncobj;

   struct amdgpu_winsys *aws;

   /* The following field aren't set for imported fences. */
   struct amdgpu_ctx *ctx;  /* submission context */
   uint32_t ip_type;
   uint64_t *user_fence_cpu_address;
   uint64_t seq_no;

   /* If the fence has been submitted. This is unsignalled for deferred fences
    * (cs->next_fence) and while an IB is still being submitted in the submit
    * thread. */
   struct util_queue_fence submitted;

   volatile int signalled;              /* bool (int for atomicity) */
   bool imported;
   uint8_t queue_index;       /* for non-imported fences */
   uint_seq_no queue_seq_no;  /* winsys-generated sequence number */
};

static inline struct amdgpu_cs_context *
amdgpu_csc_get_current(struct amdgpu_cs *acs)
{
   return &acs->csc[acs->current_csc_index];
}

static inline struct amdgpu_cs_context *
amdgpu_csc_get_submitted(struct amdgpu_cs *acs)
{
   return &acs->csc[!acs->current_csc_index];
}

static inline void
amdgpu_csc_swap(struct amdgpu_cs *acs)
{
   acs->current_csc_index = !acs->current_csc_index;
}

void amdgpu_fence_destroy(struct amdgpu_fence *fence);

static inline void amdgpu_ctx_reference(struct amdgpu_ctx **dst, struct amdgpu_ctx *src)
{
   struct amdgpu_ctx *old_dst = *dst;

   if (pipe_reference(old_dst ? &old_dst->reference : NULL,
                      src ? &src->reference : NULL)) {
      ac_drm_device *dev = old_dst->aws->dev;
      ac_drm_bo_cpu_unmap(dev, old_dst->user_fence_bo);
      ac_drm_bo_free(dev, old_dst->user_fence_bo);
      ac_drm_cs_ctx_free(dev, old_dst->ctx_handle);
      FREE(old_dst);
   }
   *dst = src;
}

static inline void amdgpu_fence_reference(struct pipe_fence_handle **dst,
                                          struct pipe_fence_handle *src)
{
   struct amdgpu_fence **adst = (struct amdgpu_fence **)dst;
   struct amdgpu_fence *asrc = (struct amdgpu_fence *)src;

   if (pipe_reference(&(*adst)->reference, &asrc->reference))
      amdgpu_fence_destroy(*adst);

   *adst = asrc;
}

/* Same as amdgpu_fence_reference, but ignore the value in *dst. */
static inline void amdgpu_fence_set_reference(struct pipe_fence_handle **dst,
                                              struct pipe_fence_handle *src)
{
   *dst = src;
   pipe_reference(NULL, &((struct amdgpu_fence *)src)->reference); /* only increment refcount */
}

/* Unreference dst, but don't assign anything. */
static inline void amdgpu_fence_drop_reference(struct pipe_fence_handle *dst)
{
   struct amdgpu_fence *adst = (struct amdgpu_fence *)dst;

   if (pipe_reference(&adst->reference, NULL)) /* only decrement refcount */
      amdgpu_fence_destroy(adst);
}

struct amdgpu_cs_buffer *
amdgpu_lookup_buffer_any_type(struct amdgpu_cs_context *csc, struct amdgpu_winsys_bo *bo);

static inline struct amdgpu_cs *
amdgpu_cs(struct radeon_cmdbuf *rcs)
{
   struct amdgpu_cs *acs = (struct amdgpu_cs*)rcs->priv;
   assert(acs);
   return acs;
}

#define get_container(member_ptr, container_type, container_member) \
   (container_type *)((char *)(member_ptr) - offsetof(container_type, container_member))

static inline bool
amdgpu_bo_is_referenced_by_cs(struct amdgpu_cs *acs,
                              struct amdgpu_winsys_bo *bo)
{
   return amdgpu_lookup_buffer_any_type(amdgpu_csc_get_current(acs), bo) != NULL;
}

static inline unsigned get_buf_list_idx(struct amdgpu_winsys_bo *bo)
{
   /* AMDGPU_BO_REAL_REUSABLE* maps to AMDGPU_BO_REAL. */
   static_assert(ARRAY_SIZE(((struct amdgpu_cs_context*)NULL)->buffer_lists) == NUM_BO_LIST_TYPES, "");
   return MIN2(bo->type, AMDGPU_BO_REAL);
}

static inline bool
amdgpu_bo_is_referenced_by_cs_with_usage(struct amdgpu_cs *acs,
                                         struct amdgpu_winsys_bo *bo,
                                         unsigned usage)
{
   struct amdgpu_cs_buffer *buffer = amdgpu_lookup_buffer_any_type(amdgpu_csc_get_current(acs), bo);

   return buffer && (buffer->usage & usage) != 0;
}

bool amdgpu_fence_wait(struct pipe_fence_handle *fence, uint64_t timeout,
                       bool absolute);
void amdgpu_cs_sync_flush(struct radeon_cmdbuf *rcs);
void amdgpu_cs_init_functions(struct amdgpu_screen_winsys *sws);

#ifdef __cplusplus
}
#endif

#endif
