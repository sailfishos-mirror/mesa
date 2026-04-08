/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>

#include "anv_private.h"

void
anv_device_update_fault_state(struct anv_device *device,
                              int device_errno)
{
   assert(device_errno != 0);

   mtx_lock(&device->fault.mutex);

   struct anv_device_fault_state *state = &device->fault.state;
   if (device_errno == ENODEV || device_errno == EIO || !state->device_status)
      state->device_status = device_errno;

   mtx_unlock(&device->fault.mutex);
}

void
anv_queue_update_fault_state(struct anv_queue *queue,
                             int queue_errno)
{
   assert(queue != NULL);
   assert(queue_errno != 0);

   struct anv_device *device = queue->device;
   mtx_lock(&device->fault.mutex);

   struct anv_device_fault_state *state = &device->fault.state;
   if (queue_errno == ENODEV || queue_errno == EIO) {
      state->device_status = queue_errno;
   } else if (!state->queue_status) {
      state->queue_status = queue_errno;
      state->queue.family = queue->vk.queue_family_index;
      state->queue.index = queue->vk.index_in_family;
      state->queue.flags = queue->vk.flags;
   }

   mtx_unlock(&device->fault.mutex);
}
