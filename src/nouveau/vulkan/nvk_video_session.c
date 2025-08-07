/*
 * Copyright © 2023 Collabora, Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_video_session.h"

#include "vk_alloc.h"
#include "nvk_device.h"
#include "nvk_cmd_buffer.h"
#include "nvk_physical_device.h"
#include "nvk_entrypoints.h"

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateVideoSessionKHR(VkDevice _device,
                          const VkVideoSessionCreateInfoKHR *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkVideoSessionKHR *pVideoSession)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);

   struct nvk_video_session *vid =
      vk_zalloc2(&dev->vk.alloc, pAllocator, sizeof(*vid), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!vid)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_video_session_init(&dev->vk, &vid->vk, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&dev->vk.alloc, pAllocator, vid);
      return result;
   }

   assert(util_bitcount(vid->vk.op) == 1);
   switch (vid->vk.op) {
   default:
      vk_video_session_finish(&vid->vk);
      vk_free2(&dev->vk.alloc, pAllocator, vid);
      return vk_error(dev, VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR);
   }

   *pVideoSession = nvk_video_session_to_handle(vid);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyVideoSessionKHR(VkDevice _device, VkVideoSessionKHR videoSession,
                           const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_video_session, vid, videoSession);

   if (vid == NULL)
      return;

   vk_video_session_finish(&vid->vk);
   vk_free2(&dev->vk.alloc, pAllocator, vid);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetVideoSessionMemoryRequirementsKHR(VkDevice _device,
   VkVideoSessionKHR videoSession,
   uint32_t *pMemoryRequirementsCount,
   VkVideoSessionMemoryRequirementsKHR *pMemoryRequirements)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_video_session, vid, videoSession);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   const uint32_t memory_type_bits = BITFIELD_MASK(pdev->mem_type_count);

   VK_OUTARRAY_MAKE_TYPED(VkVideoSessionMemoryRequirementsKHR, out,
                          pMemoryRequirements, pMemoryRequirementsCount);

   for (unsigned i = 0; i < ARRAY_SIZE(vid->mems); i++) {
      if (vid->mems[i].size_B == 0)
         continue;

      vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, m) {
         m->memoryBindIndex = i;
         m->memoryRequirements.size = vid->mems[i].size_B;
         m->memoryRequirements.alignment = vid->mems[i].align_B;
         m->memoryRequirements.memoryTypeBits = memory_type_bits;
      }
   }

   return vk_outarray_status(&out);
}

static void
copy_bind(struct nvk_vid_mem *dst, const VkBindVideoSessionMemoryInfoKHR *src)
{
   dst->mem = nvk_device_memory_from_handle(src->memory);
   dst->offset = src->memoryOffset;
   dst->size = src->memorySize;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_BindVideoSessionMemoryKHR(VkDevice _device,
   VkVideoSessionKHR videoSession,
   uint32_t videoSessionBindMemoryCount,
   const VkBindVideoSessionMemoryInfoKHR *pBindSessionMemoryInfos)
{
   VK_FROM_HANDLE(nvk_video_session, vid, videoSession);

   for (unsigned i = 0; i < videoSessionBindMemoryCount; i++) {
      copy_bind(&vid->mems[pBindSessionMemoryInfos[i].memoryBindIndex],
                &pBindSessionMemoryInfos[i]);
   }
   return VK_SUCCESS;
}
