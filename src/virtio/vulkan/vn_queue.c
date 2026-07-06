/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_queue.h"

#include "venus-protocol/vn_protocol_driver_queue.h"

#include "vn_command_buffer.h"
#include "vn_device.h"
#include "vn_feedback.h"
#include "vn_physical_device.h"
#include "vn_sync.h"
#include "vn_wsi.h"

struct vn_submit_info_pnext_fix {
   VkDeviceGroupSubmitInfo group;
   VkProtectedSubmitInfo protected;
   VkTimelineSemaphoreSubmitInfo timeline;
};

struct vn_queue_submission {
   VkStructureType batch_type;
   VkQueue queue_handle;
   uint32_t batch_count;
   union {
      const void *batches;
      const VkSubmitInfo *submit_batches;
      const VkSubmitInfo2 *submit2_batches;
      const VkBindSparseInfo *sparse_batches;
   };
   VkFence fence_handle;
   bool can_feedback;

   uint32_t cmd_count;
   uint32_t feedback_types;
   uint32_t pnext_count;
   uint32_t dev_mask_count;
   struct vn_sync_payload_external external_payload;

   /* Temporary storage allocation for submission
    *
    * A single alloc for storage is performed and the offsets inside storage
    * are set as below:
    *
    * batches
    *  - non-empty submission: copy of original batches
    *  - empty submission: a single batch for fence feedback (ffb)
    * cmds
    *  - for each batch:
    *    - copy of original batch cmds
    *    - a single cmd for query feedback (qfb)
    *    - one cmd for each signal semaphore that has feedback (sfb)
    *    - if last batch, a single cmd for ffb
    */
   struct {
      void *storage;

      union {
         void *batches;
         VkSubmitInfo *submit_batches;
         VkSubmitInfo2 *submit2_batches;
      };

      union {
         void *cmds;
         VkCommandBuffer *cmd_handles;
         VkCommandBufferSubmitInfo *cmd_infos;
      };

      struct vn_submit_info_pnext_fix *pnexts;
      uint32_t *dev_masks;
   } temp;
};

static inline uint32_t
vn_get_wait_semaphore_count(struct vn_queue_submission *submit,
                            uint32_t batch_index)
{
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      return submit->submit_batches[batch_index].waitSemaphoreCount;
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batches[batch_index].waitSemaphoreInfoCount;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      return submit->sparse_batches[batch_index].waitSemaphoreCount;
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static inline uint32_t
vn_get_signal_semaphore_count(struct vn_queue_submission *submit,
                              uint32_t batch_index)
{
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      return submit->submit_batches[batch_index].signalSemaphoreCount;
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batches[batch_index].signalSemaphoreInfoCount;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      return submit->sparse_batches[batch_index].signalSemaphoreCount;
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static inline VkSemaphore
vn_get_wait_semaphore(struct vn_queue_submission *submit,
                      uint32_t batch_index,
                      uint32_t semaphore_index)
{
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      return submit->submit_batches[batch_index]
         .pWaitSemaphores[semaphore_index];
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batches[batch_index]
         .pWaitSemaphoreInfos[semaphore_index]
         .semaphore;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      return submit->sparse_batches[batch_index]
         .pWaitSemaphores[semaphore_index];
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static inline VkSemaphore
vn_get_signal_semaphore(struct vn_queue_submission *submit,
                        uint32_t batch_index,
                        uint32_t semaphore_index)
{
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      return submit->submit_batches[batch_index]
         .pSignalSemaphores[semaphore_index];
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batches[batch_index]
         .pSignalSemaphoreInfos[semaphore_index]
         .semaphore;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      return submit->sparse_batches[batch_index]
         .pSignalSemaphores[semaphore_index];
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static inline size_t
vn_get_batch_size(struct vn_queue_submission *submit)
{
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   return submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO
             ? sizeof(VkSubmitInfo)
             : sizeof(VkSubmitInfo2);
}

static inline size_t
vn_get_cmd_size(struct vn_queue_submission *submit)
{
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   return submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO
             ? sizeof(VkCommandBuffer)
             : sizeof(VkCommandBufferSubmitInfo);
}

static inline uint32_t
vn_get_cmd_count(struct vn_queue_submission *submit, uint32_t batch_index)
{
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   return submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO
             ? submit->submit_batches[batch_index].commandBufferCount
             : submit->submit2_batches[batch_index].commandBufferInfoCount;
}

static inline const void *
vn_get_cmds(struct vn_queue_submission *submit, uint32_t batch_index)
{
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   return submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO
             ? (const void *)submit->submit_batches[batch_index]
                  .pCommandBuffers
             : (const void *)submit->submit2_batches[batch_index]
                  .pCommandBufferInfos;
}

static inline struct vn_command_buffer *
vn_get_cmd(struct vn_queue_submission *submit,
           uint32_t batch_index,
           uint32_t cmd_index)
{
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   return vn_command_buffer_from_handle(
      submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO
         ? submit->submit_batches[batch_index].pCommandBuffers[cmd_index]
         : submit->submit2_batches[batch_index]
              .pCommandBufferInfos[cmd_index]
              .commandBuffer);
}

static inline void
vn_set_temp_cmd(struct vn_queue_submission *submit,
                uint32_t cmd_index,
                VkCommandBuffer cmd_handle)
{
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
      submit->temp.cmd_infos[cmd_index] = (VkCommandBufferSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
         .commandBuffer = cmd_handle,
      };
   } else {
      submit->temp.cmd_handles[cmd_index] = cmd_handle;
   }
}

static uint64_t
vn_get_signal_semaphore_counter(struct vn_queue_submission *submit,
                                uint32_t batch_index,
                                uint32_t sem_index)
{
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO: {
      const VkTimelineSemaphoreSubmitInfo *timeline_sem_info =
         vk_find_struct_const(submit->submit_batches[batch_index].pNext,
                              TIMELINE_SEMAPHORE_SUBMIT_INFO);
      return timeline_sem_info->pSignalSemaphoreValues[sem_index];
   }
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batches[batch_index]
         .pSignalSemaphoreInfos[sem_index]
         .value;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO: {
      const VkTimelineSemaphoreSubmitInfo *timeline_sem_info =
         vk_find_struct_const(submit->sparse_batches[batch_index].pNext,
                              TIMELINE_SEMAPHORE_SUBMIT_INFO);
      return timeline_sem_info->pSignalSemaphoreValues[sem_index];
   }
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static void
vn_fix_device_group_cmd_count(struct vn_queue_submission *submit,
                              uint32_t batch_index)
{
   struct vk_queue *queue_vk = vk_queue_from_handle(submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);
   const VkSubmitInfo *src_batch = &submit->submit_batches[batch_index];
   struct vn_submit_info_pnext_fix *pnext_fix = submit->temp.pnexts;
   VkBaseOutStructure *dst =
      (void *)&submit->temp.submit_batches[batch_index];
   uint32_t new_cmd_count =
      submit->temp.submit_batches[batch_index].commandBufferCount;

   vk_foreach_struct_const(src, src_batch->pNext) {
      void *pnext = NULL;
      switch (src->sType) {
      case VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO: {
         uint32_t orig_cmd_count = 0;

         memcpy(&pnext_fix->group, src, sizeof(pnext_fix->group));

         VkDeviceGroupSubmitInfo *src_device_group =
            (VkDeviceGroupSubmitInfo *)src;
         if (src_device_group->commandBufferCount) {
            orig_cmd_count = src_device_group->commandBufferCount;
            memcpy(submit->temp.dev_masks,
                   src_device_group->pCommandBufferDeviceMasks,
                   sizeof(uint32_t) * orig_cmd_count);
         }

         /* Set the group device mask. Unlike sync2, zero means skip. */
         for (uint32_t i = orig_cmd_count; i < new_cmd_count; i++) {
            submit->temp.dev_masks[i] = dev->device_mask;
         }

         pnext_fix->group.commandBufferCount = new_cmd_count;
         pnext_fix->group.pCommandBufferDeviceMasks = submit->temp.dev_masks;
         pnext = &pnext_fix->group;
         break;
      }
      case VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO:
         memcpy(&pnext_fix->protected, src, sizeof(pnext_fix->protected));
         pnext = &pnext_fix->protected;
         break;
      case VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO:
         memcpy(&pnext_fix->timeline, src, sizeof(pnext_fix->timeline));
         pnext = &pnext_fix->timeline;
         break;
      default:
         /* The following structs are not supported by venus so are not
          * handled here. VkAmigoProfilingSubmitInfoSEC,
          * VkD3D12FenceSubmitInfoKHR, VkFrameBoundaryEXT,
          * VkLatencySubmissionPresentIdNV, VkPerformanceQuerySubmitInfoKHR,
          * VkWin32KeyedMutexAcquireReleaseInfoKHR,
          * VkWin32KeyedMutexAcquireReleaseInfoNV
          */
         break;
      }

      if (pnext) {
         dst->pNext = pnext;
         dst = pnext;
      }
   }
   submit->temp.pnexts++;
   submit->temp.dev_masks += new_cmd_count;
}

static VkResult
vn_queue_submission_fix_batch_semaphores(struct vn_queue_submission *submit,
                                         uint32_t batch_index)
{
   struct vk_queue *queue_vk = vk_queue_from_handle(submit->queue_handle);
   VkDevice dev_handle = vk_device_to_handle(queue_vk->base.device);

   const uint32_t wait_count =
      vn_get_wait_semaphore_count(submit, batch_index);
   for (uint32_t i = 0; i < wait_count; i++) {
      VkSemaphore sem_handle = vn_get_wait_semaphore(submit, batch_index, i);
      if (vn_semaphore_is_imported(sem_handle) &&
          !vn_semaphore_wait_imported(dev_handle, sem_handle))
         return VK_ERROR_DEVICE_LOST;
   }

   return VK_SUCCESS;
}

static void
vn_queue_submission_count_batch_feedback(struct vn_queue_submission *submit,
                                         uint32_t batch_index)
{
   const uint32_t signal_count =
      vn_get_signal_semaphore_count(submit, batch_index);
   uint32_t extra_cmd_count = 0;
   uint32_t feedback_types = 0;

   for (uint32_t i = 0; i < signal_count; i++) {
      struct vn_semaphore *sem = vn_semaphore_from_handle(
         vn_get_signal_semaphore(submit, batch_index, i));
      if (vn_sync_feedback_enabled(&sem->feedback)) {
         if (submit->can_feedback) {
            feedback_types |= VN_FEEDBACK_TYPE_SEMAPHORE;
            extra_cmd_count++;
         } else {
            const uint64_t counter =
               vn_get_signal_semaphore_counter(submit, batch_index, i);
            vn_sync_feedback_suspend(&sem->feedback, counter);
         }
      }
   }

   if (submit->batch_type != VK_STRUCTURE_TYPE_BIND_SPARSE_INFO) {
      const uint32_t cmd_count = vn_get_cmd_count(submit, batch_index);
      for (uint32_t i = 0; i < cmd_count; i++) {
         struct vn_command_buffer *cmd = vn_get_cmd(submit, batch_index, i);
         if (!list_is_empty(&cmd->builder.query_records))
            feedback_types |= VN_FEEDBACK_TYPE_QUERY;

         /* If a cmd that was submitted previously and already has a feedback
          * cmd linked, as long as
          * VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT was not set we can
          * assume it has completed execution and is no longer in the pending
          * state so its safe to recycle the old feedback command.
          */
         if (cmd->linked_qfb_cmd) {
            assert(!cmd->builder.is_simultaneous);

            vn_query_feedback_cmd_free(cmd->linked_qfb_cmd);
            cmd->linked_qfb_cmd = NULL;
         }
      }
      if (feedback_types & VN_FEEDBACK_TYPE_QUERY)
         extra_cmd_count++;

      if (submit->feedback_types & VN_FEEDBACK_TYPE_FENCE &&
          batch_index == submit->batch_count - 1) {
         feedback_types |= VN_FEEDBACK_TYPE_FENCE;
         extra_cmd_count++;
      }

      if (feedback_types)
         extra_cmd_count += cmd_count;
   }

   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO &&
       extra_cmd_count) {
      const VkDeviceGroupSubmitInfo *device_group = vk_find_struct_const(
         submit->submit_batches[batch_index].pNext, DEVICE_GROUP_SUBMIT_INFO);
      if (device_group) {
         submit->pnext_count++;
         submit->dev_mask_count += extra_cmd_count;
      }
   }

   submit->feedback_types |= feedback_types;
   submit->cmd_count += extra_cmd_count;
}

static VkResult
vn_queue_submission_prepare(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue_handle);
   struct vn_fence *fence = vn_fence_from_handle(submit->fence_handle);

   if (fence && vn_sync_feedback_enabled(&fence->feedback)) {
      if (submit->can_feedback)
         submit->feedback_types |= VN_FEEDBACK_TYPE_FENCE;
      else
         vn_sync_feedback_suspend(&fence->feedback, fence->signal_counter);
   }

   submit->external_payload.ring_idx = queue->ring_idx;

   for (uint32_t i = 0; i < submit->batch_count; i++) {
      VkResult result = vn_queue_submission_fix_batch_semaphores(submit, i);
      if (result != VK_SUCCESS)
         return result;

      vn_queue_submission_count_batch_feedback(submit, i);
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_alloc_storage(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue_handle);

   if (!submit->feedback_types)
      return VK_SUCCESS;

   /* for original batches or a new batch to hold feedback fence cmd */
   const size_t total_batch_size =
      vn_get_batch_size(submit) * MAX2(submit->batch_count, 1);
   /* for fence, timeline semaphore and query feedback cmds */
   const size_t total_cmd_size =
      vn_get_cmd_size(submit) * MAX2(submit->cmd_count, 1);
   /* for fixing command buffer counts in device group info, if it exists */
   const size_t total_pnext_size =
      submit->pnext_count * sizeof(struct vn_submit_info_pnext_fix);
   const size_t total_dev_mask_size =
      submit->dev_mask_count * sizeof(uint32_t);
   submit->temp.storage = vn_cached_storage_get(
      &queue->storage, total_batch_size + total_cmd_size + total_pnext_size +
                          total_dev_mask_size);
   if (!submit->temp.storage)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   submit->temp.batches = submit->temp.storage;
   submit->temp.cmds = submit->temp.storage + total_batch_size;
   submit->temp.pnexts =
      submit->temp.storage + total_batch_size + total_cmd_size;
   submit->temp.dev_masks = submit->temp.storage + total_batch_size +
                            total_cmd_size + total_pnext_size;

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_get_resolved_query_records(
   struct vn_queue_submission *submit,
   uint32_t batch_index,
   struct vn_feedback_cmd_pool *fb_cmd_pool,
   struct list_head *resolved_records)
{
   struct vn_command_pool *cmd_pool =
      vn_command_pool_from_handle(fb_cmd_pool->pool_handle);
   struct list_head dropped_records;
   VkResult result = VK_SUCCESS;

   list_inithead(resolved_records);
   list_inithead(&dropped_records);
   const uint32_t cmd_count = vn_get_cmd_count(submit, batch_index);
   for (uint32_t i = 0; i < cmd_count; i++) {
      struct vn_command_buffer *cmd = vn_get_cmd(submit, batch_index, i);

      list_for_each_entry(struct vn_cmd_query_record, record,
                          &cmd->builder.query_records, head) {
         if (!record->copy) {
            list_for_each_entry_safe(struct vn_cmd_query_record, prev,
                                     resolved_records, head) {
               /* If we previously added a query feedback that is now getting
                * reset, remove it since it is now a no-op and the deferred
                * feedback copy will cause a hang waiting for the reset query
                * to become available.
                */
               if (prev->copy && prev->query_pool == record->query_pool &&
                   prev->query >= record->query &&
                   prev->query < record->query + record->query_count)
                  list_move_to(&prev->head, &dropped_records);
            }
         }

         simple_mtx_lock(&fb_cmd_pool->mutex);
         struct vn_cmd_query_record *curr = vn_cmd_pool_alloc_query_record(
            cmd_pool, record->query_pool, record->query, record->query_count,
            record->copy);
         simple_mtx_unlock(&fb_cmd_pool->mutex);

         if (!curr) {
            list_splicetail(resolved_records, &dropped_records);
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out_free_dropped_records;
         }

         list_addtail(&curr->head, resolved_records);
      }
   }

   /* further resolve to batch sequential queries */
   struct vn_cmd_query_record *curr =
      list_first_entry(resolved_records, struct vn_cmd_query_record, head);
   list_for_each_entry_safe(struct vn_cmd_query_record, next,
                            resolved_records, head) {
      if (curr->query_pool == next->query_pool && curr->copy == next->copy) {
         if (curr->query + curr->query_count == next->query) {
            curr->query_count += next->query_count;
            list_move_to(&next->head, &dropped_records);
         } else if (curr->query == next->query + next->query_count) {
            curr->query = next->query;
            curr->query_count += next->query_count;
            list_move_to(&next->head, &dropped_records);
         } else {
            curr = next;
         }
      } else {
         curr = next;
      }
   }

out_free_dropped_records:
   simple_mtx_lock(&fb_cmd_pool->mutex);
   vn_cmd_pool_free_query_records(cmd_pool, &dropped_records);
   simple_mtx_unlock(&fb_cmd_pool->mutex);
   return result;
}

static VkResult
vn_queue_submission_add_query_feedback(struct vn_queue_submission *submit,
                                       uint32_t batch_index,
                                       uint32_t *new_cmd_count)
{
   struct vk_queue *queue_vk = vk_queue_from_handle(submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);
   VkResult result;

   struct vn_feedback_cmd_pool *fb_cmd_pool = NULL;
   for (uint32_t i = 0; i < dev->queue_family_count; i++) {
      if (dev->queue_families[i] == queue_vk->queue_family_index) {
         fb_cmd_pool = &dev->fb_cmd_pools[i];
         break;
      }
   }
   assert(fb_cmd_pool);

   struct list_head resolved_records;
   result = vn_queue_submission_get_resolved_query_records(
      submit, batch_index, fb_cmd_pool, &resolved_records);
   if (result != VK_SUCCESS)
      return result;

   /* currently the reset query is always recorded */
   assert(!list_is_empty(&resolved_records));
   struct vn_query_feedback_cmd *qfb_cmd;
   result = vn_query_feedback_cmd_alloc(vn_device_to_handle(dev), fb_cmd_pool,
                                        &resolved_records, &qfb_cmd);
   if (result == VK_SUCCESS) {
      /* link query feedback cmd lifecycle with a cmd in the original batch so
       * that the feedback cmd can be reset and recycled when that cmd gets
       * reset/freed.
       *
       * Avoid cmd buffers with VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT
       * since we don't know if all its instances have completed execution.
       * Should be rare enough to just log and leak the feedback cmd.
       */
      bool found_companion_cmd = false;
      const uint32_t cmd_count = vn_get_cmd_count(submit, batch_index);
      for (uint32_t i = 0; i < cmd_count; i++) {
         struct vn_command_buffer *cmd = vn_get_cmd(submit, batch_index, i);
         if (!cmd->builder.is_simultaneous) {
            cmd->linked_qfb_cmd = qfb_cmd;
            found_companion_cmd = true;
            break;
         }
      }
      if (!found_companion_cmd)
         vn_log(dev->instance, "WARN: qfb cmd has leaked!");

      vn_set_temp_cmd(submit, (*new_cmd_count)++, qfb_cmd->cmd_handle);
   }

   simple_mtx_lock(&fb_cmd_pool->mutex);
   vn_cmd_pool_free_query_records(
      vn_command_pool_from_handle(fb_cmd_pool->pool_handle),
      &resolved_records);
   simple_mtx_unlock(&fb_cmd_pool->mutex);

   return result;
}

static VkResult
vn_queue_submission_add_semaphore_feedback(struct vn_queue_submission *submit,
                                           uint32_t batch_index,
                                           uint32_t signal_index,
                                           uint32_t *new_cmd_count)
{
   struct vn_semaphore *sem = vn_semaphore_from_handle(
      vn_get_signal_semaphore(submit, batch_index, signal_index));
   if (!vn_sync_feedback_enabled(&sem->feedback))
      return VK_SUCCESS;

   VK_FROM_HANDLE(vk_queue, queue_vk, submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);
   const uint64_t counter =
      vn_get_signal_semaphore_counter(submit, batch_index, signal_index);

   VkCommandBuffer sfb_cmd_handle = vn_sync_feedback_command(
      dev, &sem->feedback, queue_vk->queue_family_index, counter);
   if (sfb_cmd_handle == VK_NULL_HANDLE)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_set_temp_cmd(submit, (*new_cmd_count)++, sfb_cmd_handle);
   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_add_fence_feedback(struct vn_queue_submission *submit,
                                       uint32_t batch_index,
                                       uint32_t *new_cmd_count)
{
   VK_FROM_HANDLE(vk_queue, queue_vk, submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);
   struct vn_fence *fence = vn_fence_from_handle(submit->fence_handle);

   VkCommandBuffer ffb_cmd_handle = vn_sync_feedback_command(
      dev, &fence->feedback, queue_vk->queue_family_index,
      fence->signal_counter);
   if (ffb_cmd_handle == VK_NULL_HANDLE)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_set_temp_cmd(submit, (*new_cmd_count)++, ffb_cmd_handle);
   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_add_feedback_cmds(struct vn_queue_submission *submit,
                                      uint32_t batch_index,
                                      uint32_t feedback_types)
{
   VkResult result;
   uint32_t new_cmd_count = vn_get_cmd_count(submit, batch_index);

   if (feedback_types & VN_FEEDBACK_TYPE_QUERY) {
      result = vn_queue_submission_add_query_feedback(submit, batch_index,
                                                      &new_cmd_count);
      if (result != VK_SUCCESS)
         return result;
   }

   if (feedback_types & VN_FEEDBACK_TYPE_SEMAPHORE) {
      const uint32_t signal_count =
         vn_get_signal_semaphore_count(submit, batch_index);
      for (uint32_t i = 0; i < signal_count; i++) {
         result = vn_queue_submission_add_semaphore_feedback(
            submit, batch_index, i, &new_cmd_count);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   if (feedback_types & VN_FEEDBACK_TYPE_FENCE) {
      result = vn_queue_submission_add_fence_feedback(submit, batch_index,
                                                      &new_cmd_count);
      if (result != VK_SUCCESS)
         return result;
   }

   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
      VkSubmitInfo2 *batch = &submit->temp.submit2_batches[batch_index];
      batch->pCommandBufferInfos = submit->temp.cmd_infos;
      batch->commandBufferInfoCount = new_cmd_count;
   } else {
      VkSubmitInfo *batch = &submit->temp.submit_batches[batch_index];
      batch->pCommandBuffers = submit->temp.cmd_handles;
      batch->commandBufferCount = new_cmd_count;

      const VkDeviceGroupSubmitInfo *device_group = vk_find_struct_const(
         submit->submit_batches[batch_index].pNext, DEVICE_GROUP_SUBMIT_INFO);
      if (device_group)
         vn_fix_device_group_cmd_count(submit, batch_index);
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_setup_batch(struct vn_queue_submission *submit,
                                uint32_t batch_index)
{
   uint32_t feedback_types = 0;
   uint32_t extra_cmd_count = 0;

   const uint32_t signal_count =
      vn_get_signal_semaphore_count(submit, batch_index);
   for (uint32_t i = 0; i < signal_count; i++) {
      struct vn_semaphore *sem = vn_semaphore_from_handle(
         vn_get_signal_semaphore(submit, batch_index, i));
      if (vn_sync_feedback_enabled(&sem->feedback) && submit->can_feedback) {
         feedback_types |= VN_FEEDBACK_TYPE_SEMAPHORE;
         extra_cmd_count++;
      }
   }

   const uint32_t cmd_count = vn_get_cmd_count(submit, batch_index);
   for (uint32_t i = 0; i < cmd_count; i++) {
      struct vn_command_buffer *cmd = vn_get_cmd(submit, batch_index, i);
      if (!list_is_empty(&cmd->builder.query_records)) {
         feedback_types |= VN_FEEDBACK_TYPE_QUERY;
         extra_cmd_count++;
         break;
      }
   }

   if (submit->feedback_types & VN_FEEDBACK_TYPE_FENCE &&
       batch_index == submit->batch_count - 1) {
      feedback_types |= VN_FEEDBACK_TYPE_FENCE;
      extra_cmd_count++;
   }

   /* If the batch has qfb, sfb or ffb, copy the original commands and append
    * feedback cmds.
    */
   if (feedback_types) {
      const size_t cmd_size = vn_get_cmd_size(submit);
      const size_t total_cmd_size = cmd_count * cmd_size;
      /* copy only needed for non-empty batches */
      if (total_cmd_size) {
         memcpy(submit->temp.cmds, vn_get_cmds(submit, batch_index),
                total_cmd_size);
      }

      VkResult result = vn_queue_submission_add_feedback_cmds(
         submit, batch_index, feedback_types);
      if (result != VK_SUCCESS)
         return result;

      /* advance the temp cmds for working on next batch cmds */
      submit->temp.cmds += total_cmd_size + (extra_cmd_count * cmd_size);
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_setup_batches(struct vn_queue_submission *submit)
{
   if (!submit->feedback_types)
      return VK_SUCCESS;

   assert(submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2 ||
          submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO);

   /* For a submission that is:
    * - non-empty: copy batches for adding feedbacks
    * - empty: initialize a batch for fence feedback
    */
   if (submit->batch_count) {
      memcpy(submit->temp.batches, submit->batches,
             vn_get_batch_size(submit) * submit->batch_count);
   } else {
      assert(submit->feedback_types & VN_FEEDBACK_TYPE_FENCE);
      if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
         submit->temp.submit2_batches[0] = (VkSubmitInfo2){
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
         };
      } else {
         submit->temp.submit_batches[0] = (VkSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         };
      }
      submit->batch_count = 1;
      submit->batches = submit->temp.batches;
   }

   for (uint32_t i = 0; i < submit->batch_count; i++) {
      VkResult result = vn_queue_submission_setup_batch(submit, i);
      if (result != VK_SUCCESS)
         return result;
   }

   submit->batches = submit->temp.batches;

   return VK_SUCCESS;
}

static void
vn_queue_submission_cleanup_fence_feedback(struct vn_queue_submission *submit)
{
   struct vk_queue *queue_vk = vk_queue_from_handle(submit->queue_handle);
   VkDevice dev_handle = vk_device_to_handle(queue_vk->base.device);

   if (submit->fence_handle == VK_NULL_HANDLE)
      return;

   struct vn_fence *fence = vn_fence_from_handle(submit->fence_handle);
   if (!vn_sync_feedback_enabled(&fence->feedback))
      return;

   /* sfb pending cmds are recycled when signaled counter is updated */
   vn_GetFenceStatus(dev_handle, submit->fence_handle);
}

static void
vn_queue_submission_cleanup_semaphore_feedback(
   struct vn_queue_submission *submit)
{
   struct vk_queue *queue_vk = vk_queue_from_handle(submit->queue_handle);
   VkDevice dev_handle = vk_device_to_handle(queue_vk->base.device);

   for (uint32_t i = 0; i < submit->batch_count; i++) {
      const uint32_t wait_count = vn_get_wait_semaphore_count(submit, i);
      for (uint32_t j = 0; j < wait_count; j++) {
         VkSemaphore sem_handle = vn_get_wait_semaphore(submit, i, j);
         struct vn_semaphore *sem = vn_semaphore_from_handle(sem_handle);
         if (!vn_sync_feedback_enabled(&sem->feedback))
            continue;

         /* sfb pending cmds are recycled when signaled counter is updated */
         uint64_t counter = 0;
         vn_GetSemaphoreCounterValue(dev_handle, sem_handle, &counter);
      }

      const uint32_t signal_count = vn_get_signal_semaphore_count(submit, i);
      for (uint32_t j = 0; j < signal_count; j++) {
         VkSemaphore sem_handle = vn_get_signal_semaphore(submit, i, j);
         struct vn_semaphore *sem = vn_semaphore_from_handle(sem_handle);
         if (!vn_sync_feedback_enabled(&sem->feedback))
            continue;

         /* sfb pending cmds are recycled when signaled counter is updated */
         uint64_t counter = 0;
         vn_GetSemaphoreCounterValue(dev_handle, sem_handle, &counter);
      }
   }
}

static void
vn_queue_submission_cleanup(struct vn_queue_submission *submit)
{
   /* TODO clean up pending src feedbacks on failure? */
   if (submit->feedback_types & VN_FEEDBACK_TYPE_SEMAPHORE)
      vn_queue_submission_cleanup_semaphore_feedback(submit);

   if (submit->feedback_types & VN_FEEDBACK_TYPE_FENCE)
      vn_queue_submission_cleanup_fence_feedback(submit);
}

static VkResult
vn_queue_submission_prepare_submit(struct vn_queue_submission *submit)
{
   VkResult result = vn_queue_submission_prepare(submit);
   if (result != VK_SUCCESS)
      return result;

   result = vn_queue_submission_alloc_storage(submit);
   if (result != VK_SUCCESS)
      return result;

   result = vn_queue_submission_setup_batches(submit);
   if (result != VK_SUCCESS) {
      vn_queue_submission_cleanup(submit);
      return result;
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submit(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue->base.vk.base.device);
   struct vn_instance *instance = dev->instance;
   VkResult result;

   /* To ensure external components waiting on the correct fence payload,
    * below sync primitives must be installed after the submission:
    * - explicit fencing: sync file export
    *
    * We enforce above via an asynchronous vkQueueSubmit(2) via ring followed
    * by an asynchronous renderer submission to wait for the ring submission:
    * - fence is an external fence
    * - has an external signal semaphore
    */
   result = vn_queue_submission_prepare_submit(submit);
   if (result != VK_SUCCESS)
      return vn_error(instance, result);

   /* skip no-op submit */
   if (!submit->batch_count && submit->fence_handle == VK_NULL_HANDLE)
      return VK_SUCCESS;

   if (VN_PERF(NO_ASYNC_QUEUE_SUBMIT)) {
      if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
         result = vn_call_vkQueueSubmit2(
            dev->primary_ring, submit->queue_handle, submit->batch_count,
            submit->submit2_batches, submit->fence_handle);
      } else if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
         result = vn_call_vkQueueSubmit(
            dev->primary_ring, submit->queue_handle, submit->batch_count,
            submit->submit_batches, submit->fence_handle);
      } else {
         result = vn_call_vkQueueBindSparse(
            dev->primary_ring, submit->queue_handle, submit->batch_count,
            submit->sparse_batches, submit->fence_handle);
      }

      if (result != VK_SUCCESS) {
         vn_queue_submission_cleanup(submit);
         return vn_error(instance, result);
      }
   } else {
      struct vn_ring_submit_command ring_submit;
      if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
         vn_submit_vkQueueSubmit2(
            dev->primary_ring, 0, submit->queue_handle, submit->batch_count,
            submit->submit2_batches, submit->fence_handle, &ring_submit);
      } else if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
         vn_submit_vkQueueSubmit(dev->primary_ring, 0, submit->queue_handle,
                                 submit->batch_count, submit->submit_batches,
                                 submit->fence_handle, &ring_submit);
      } else {
         vn_submit_vkQueueBindSparse(
            dev->primary_ring, 0, submit->queue_handle, submit->batch_count,
            submit->sparse_batches, submit->fence_handle, &ring_submit);
      }
      if (!ring_submit.ring_seqno_valid) {
         vn_queue_submission_cleanup(submit);
         return vn_error(instance, VK_ERROR_DEVICE_LOST);
      }
      submit->external_payload.ring_seqno_valid = true;
      submit->external_payload.ring_seqno = ring_submit.ring_seqno;
   }

   /* If external fence, track the submission's ring_idx to facilitate
    * sync_file export.
    *
    * Imported syncs don't need a proxy renderer sync on subsequent export,
    * because an fd is already available.
    */
   struct vn_fence *fence = vn_fence_from_handle(submit->fence_handle);
   if (fence && fence->is_external) {
      assert(fence->payload->type == VN_SYNC_TYPE_DEVICE_ONLY);
      fence->external_payload = submit->external_payload;
   }

   for (uint32_t i = 0; i < submit->batch_count; i++) {
      const uint32_t signal_count = vn_get_signal_semaphore_count(submit, i);
      for (uint32_t j = 0; j < signal_count; j++) {
         struct vn_semaphore *sem =
            vn_semaphore_from_handle(vn_get_signal_semaphore(submit, i, j));
         if (sem->is_external) {
            assert(sem->payload->type == VN_SYNC_TYPE_DEVICE_ONLY);
            sem->external_payload = submit->external_payload;
         }
      }
   }

   vn_queue_submission_cleanup(submit);

   return VK_SUCCESS;
}

static inline bool
vn_queue_can_feedback(VkQueue queue_handle)
{
   struct vn_queue *queue = vn_queue_from_handle(queue_handle);
   return queue->can_feedback;
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_QueueSubmit(VkQueue queue,
               uint32_t submitCount,
               const VkSubmitInfo *pSubmits,
               VkFence fence)
{
   VN_TRACE_FUNC();

   vn_tls_set_async_pipeline_create();
   vn_wsi_flush(queue);

   for (uint32_t i = 0; i < submitCount; i++) {
      struct vn_queue_submission submit = {
         .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .queue_handle = queue,
         .batch_count = 1,
         .submit_batches = &pSubmits[i],
         .fence_handle = i == submitCount - 1 ? fence : VK_NULL_HANDLE,
         .can_feedback = vn_queue_can_feedback(queue),
      };
      VkResult result = vn_queue_submit(&submit);
      if (result != VK_SUCCESS)
         return result;
   }

   if (!submitCount) {
      return vn_queue_submit(&(struct vn_queue_submission){
         .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .queue_handle = queue,
         .fence_handle = fence,
         .can_feedback = vn_queue_can_feedback(queue),
      });
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submit_2_to_1(struct vn_device *dev,
                       VkQueue queue_handle,
                       const VkSubmitInfo2 *submit,
                       VkFence fence_handle)
{
   VkResult result;
   const void *pnext = NULL;

   assert(submit);

   VkProtectedSubmitInfo _protected;
   VkDeviceGroupSubmitInfo _group;
   VkTimelineSemaphoreSubmitInfo _timeline;

   STACK_ARRAY(VkSemaphore, _wait_sem_handles,
               submit->waitSemaphoreInfoCount);
   STACK_ARRAY(VkPipelineStageFlags, _wait_stages,
               submit->waitSemaphoreInfoCount);
   STACK_ARRAY(uint32_t, _wait_dev_indices, submit->waitSemaphoreInfoCount);
   STACK_ARRAY(uint64_t, _wait_values, submit->waitSemaphoreInfoCount);
   STACK_ARRAY(VkCommandBuffer, _cmd_handles, submit->commandBufferInfoCount);
   STACK_ARRAY(uint32_t, _cmd_dev_indices, submit->commandBufferInfoCount);
   STACK_ARRAY(VkSemaphore, _signal_sem_handles,
               submit->signalSemaphoreInfoCount);
   STACK_ARRAY(uint32_t, _signal_dev_indices,
               submit->signalSemaphoreInfoCount);
   STACK_ARRAY(uint64_t, _signal_values, submit->signalSemaphoreInfoCount);

   if (submit->flags & VK_SUBMIT_PROTECTED_BIT) {
      _protected = (VkProtectedSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO,
         .pNext = pnext,
         .protectedSubmit = VK_TRUE,
      };
      pnext = &_protected;
   }

   if (dev->device_mask > 1) {
      for (uint32_t i = 0; i < submit->waitSemaphoreInfoCount; i++) {
         _wait_dev_indices[i] = submit->pWaitSemaphoreInfos[i].deviceIndex;
      }
      for (uint32_t i = 0; i < submit->commandBufferInfoCount; i++) {
         _cmd_dev_indices[i] = submit->pCommandBufferInfos[i].deviceMask;
      }
      for (uint32_t i = 0; i < submit->signalSemaphoreInfoCount; i++) {
         _signal_dev_indices[i] = submit->pSignalSemaphoreInfos[i].deviceIndex;
      }
      _group = (VkDeviceGroupSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO,
         .pNext = pnext,
         .waitSemaphoreCount = submit->waitSemaphoreInfoCount,
         .pWaitSemaphoreDeviceIndices = _wait_dev_indices,
         .commandBufferCount = submit->commandBufferInfoCount,
         .pCommandBufferDeviceMasks = _cmd_dev_indices,
         .signalSemaphoreCount = submit->signalSemaphoreInfoCount,
         .pSignalSemaphoreDeviceIndices = _signal_dev_indices,
      };
      pnext = &_group;
   }

   bool has_wait_timeline_sem = false;
   for (uint32_t i = 0; i < submit->waitSemaphoreInfoCount; i++) {
      _wait_sem_handles[i] = submit->pWaitSemaphoreInfos[i].semaphore;
      _wait_stages[i] = submit->pWaitSemaphoreInfos[i].stageMask
                           ? submit->pWaitSemaphoreInfos[i].stageMask
                           : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

      VK_FROM_HANDLE(vn_semaphore, sem, _wait_sem_handles[i]);
      has_wait_timeline_sem |= sem->type == VK_SEMAPHORE_TYPE_TIMELINE;
   }
   if (has_wait_timeline_sem) {
      for (uint32_t i = 0; i < submit->waitSemaphoreInfoCount; i++)
         _wait_values[i] = submit->pWaitSemaphoreInfos[i].value;
   }

   bool has_signal_timeline_sem = false;
   for (uint32_t i = 0; i < submit->signalSemaphoreInfoCount; i++) {
      _signal_sem_handles[i] = submit->pSignalSemaphoreInfos[i].semaphore;

      VK_FROM_HANDLE(vn_semaphore, sem, _signal_sem_handles[i]);
      has_signal_timeline_sem |= sem->type == VK_SEMAPHORE_TYPE_TIMELINE;
   }
   if (has_signal_timeline_sem) {
      for (uint32_t i = 0; i < submit->signalSemaphoreInfoCount; i++)
         _signal_values[i] = submit->pSignalSemaphoreInfos[i].value;
   }

   if (has_wait_timeline_sem || has_signal_timeline_sem) {
      _timeline = (VkTimelineSemaphoreSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
         .pNext = pnext,
         .waitSemaphoreValueCount =
            has_wait_timeline_sem ? submit->waitSemaphoreInfoCount : 0,
         .pWaitSemaphoreValues = _wait_values,
         .signalSemaphoreValueCount =
            has_signal_timeline_sem ? submit->signalSemaphoreInfoCount : 0,
         .pSignalSemaphoreValues = _signal_values,
      };
      pnext = &_timeline;
   }

   for (uint32_t i = 0; i < submit->commandBufferInfoCount; i++)
      _cmd_handles[i] = submit->pCommandBufferInfos[i].commandBuffer;

   const VkSubmitInfo _submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = pnext,
      .waitSemaphoreCount = submit->waitSemaphoreInfoCount,
      .pWaitSemaphores = _wait_sem_handles,
      .pWaitDstStageMask = _wait_stages,
      .commandBufferCount = submit->commandBufferInfoCount,
      .pCommandBuffers = _cmd_handles,
      .signalSemaphoreCount = submit->signalSemaphoreInfoCount,
      .pSignalSemaphores = _signal_sem_handles,
   };
   result = vn_queue_submit(&(struct vn_queue_submission){
      .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .queue_handle = queue_handle,
      .batch_count = 1,
      .submit_batches = &_submit,
      .fence_handle = fence_handle,
      .can_feedback = vn_queue_can_feedback(queue_handle),
   });

   STACK_ARRAY_FINISH(_wait_sem_handles);
   STACK_ARRAY_FINISH(_wait_stages);
   STACK_ARRAY_FINISH(_wait_dev_indices);
   STACK_ARRAY_FINISH(_wait_values);
   STACK_ARRAY_FINISH(_cmd_handles);
   STACK_ARRAY_FINISH(_cmd_dev_indices);
   STACK_ARRAY_FINISH(_signal_sem_handles);
   STACK_ARRAY_FINISH(_signal_dev_indices);
   STACK_ARRAY_FINISH(_signal_values);

   return result;
}

static VkResult
vn_queue_submit_2(VkQueue queue_handle,
                  const VkSubmitInfo2 *batch,
                  VkFence fence_handle)
{
   VK_FROM_HANDLE(vk_queue, queue_vk, queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);

   assert(batch);

   if (!dev->has_sync2) {
      VN_TRACE_SCOPE("2->1");
      return vn_queue_submit_2_to_1(dev, queue_handle, batch, fence_handle);
   }

   struct vn_queue_submission submit = {
      .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .queue_handle = queue_handle,
      .batch_count = 1,
      .submit2_batches = batch,
      .fence_handle = fence_handle,
      .can_feedback = vn_queue_can_feedback(queue_handle),
   };
   return vn_queue_submit(&submit);
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_QueueSubmit2(VkQueue queue,
                uint32_t submitCount,
                const VkSubmitInfo2 *pSubmits,
                VkFence fence)
{
   VN_TRACE_FUNC();

   vn_tls_set_async_pipeline_create();
   vn_wsi_flush(queue);

   for (uint32_t i = 0; i < submitCount; i++) {
      VkResult result = vn_queue_submit_2(
         queue, &pSubmits[i], i == submitCount - 1 ? fence : VK_NULL_HANDLE);
      if (result != VK_SUCCESS)
         return result;
   }

   if (!submitCount) {
      return vn_queue_submit(&(struct vn_queue_submission){
         .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .queue_handle = queue,
         .fence_handle = fence,
         .can_feedback = vn_queue_can_feedback(queue),
      });
   }

   return vn_wsi_fence_wait(queue);
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_QueueBindSparse(VkQueue queue,
                   uint32_t bindInfoCount,
                   const VkBindSparseInfo *pBindInfo,
                   VkFence fence)
{
   VN_TRACE_FUNC();

   vn_wsi_flush(queue);

   for (uint32_t i = 0; i < bindInfoCount; i++) {
      struct vn_queue_submission submit = {
         .batch_type = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,
         .queue_handle = queue,
         .batch_count = 1,
         .sparse_batches = &pBindInfo[i],
         .fence_handle = i == bindInfoCount - 1 ? fence : VK_NULL_HANDLE,
      };
      VkResult result = vn_queue_submit(&submit);
      if (result != VK_SUCCESS)
         return result;
   }

   if (!bindInfoCount) {
      return vn_queue_submit(&(struct vn_queue_submission){
         .batch_type = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,
         .queue_handle = queue,
         .fence_handle = fence,
      });
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_QueueWaitIdle(VkQueue _queue)
{
   VN_TRACE_FUNC();
   struct vn_queue *queue = vn_queue_from_handle(_queue);
   VkDevice dev_handle = vk_device_to_handle(queue->base.vk.base.device);
   struct vn_device *dev = vn_device_from_handle(dev_handle);
   VkResult result;

   vn_wsi_flush(_queue);

   /* lazily create queue wait fence for queue idle waiting */
   if (queue->wait_fence == VK_NULL_HANDLE) {
      const VkFenceCreateInfo create_info = {
         .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
         .flags = 0,
      };
      result =
         vn_CreateFence(dev_handle, &create_info, NULL, &queue->wait_fence);
      if (result != VK_SUCCESS)
         return result;
   }

   /* ensure the idle wait occurs after renderer fence submit */
   vn_ring_roundtrip(dev->primary_ring);

   result = vn_queue_submit(&(struct vn_queue_submission){
      .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .queue_handle = _queue,
      .fence_handle = queue->wait_fence,
      .can_feedback = vn_queue_can_feedback(_queue),
   });
   if (result != VK_SUCCESS)
      return result;

   result =
      vn_WaitForFences(dev_handle, 1, &queue->wait_fence, true, UINT64_MAX);
   vn_ResetFences(dev_handle, 1, &queue->wait_fence);

   return vn_result(dev->instance, result);
}
