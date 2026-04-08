/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>

#include "anv_private.h"

static inline void
anv_device_print_vm_faults(struct anv_device *device)
{
   struct intel_pagefault_buffer *faults =
      anv_device_alloc_get_vm_faults(device);

   if (!faults)
      return;

   for (unsigned i = 0; i < faults->size; ++i) {
      mesa_loge("[GPU-VM-FAULT] Page Address: 0x%016"PRIx64", "
                "Page Size: 0x%04x, Access: %s, Type: %s, Level: %s",
                 faults->items[i].address, faults->items[i].precision,
                 intel_pagefault_access_to_string(faults->items[i].access),
                 intel_pagefault_type_to_string(faults->items[i].type),
                 intel_pagefault_level_to_string(faults->items[i].level));
   }

   free(faults);
}

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
   bool print_vm_faults = false;

   mtx_lock(&device->fault.mutex);

   struct anv_device_fault_state *state = &device->fault.state;
   if (queue_errno == ENODEV || queue_errno == EIO) {
      state->device_status = queue_errno;
   } else if (!state->queue_status) {
      state->queue_status = queue_errno;
      state->queue.family = queue->vk.queue_family_index;
      state->queue.index = queue->vk.index_in_family;
      state->queue.flags = queue->vk.flags;
      print_vm_faults = queue_errno == ECANCELED;
   }

   mtx_unlock(&device->fault.mutex);

   if (print_vm_faults)
      anv_device_print_vm_faults(device);
}
