/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "venus-protocol/vn_protocol_driver_descriptor_heap.h"

#include "vn_device.h"

/* descriptor heap commands */

VKAPI_ATTR VkResult VKAPI_CALL
vn_WriteSamplerDescriptorsEXT(VkDevice device,
                              uint32_t samplerCount,
                              const VkSamplerCreateInfo *pSamplers,
                              const VkHostAddressRangeEXT *pDescriptors)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO move out from primary ring? */
   for (uint32_t i = 0; i < samplerCount; i++) {
      VkResult result = vn_call_vkWriteSamplerDescriptorMESA(
         dev->primary_ring, device, &pSamplers[i], pDescriptors[i].size,
         pDescriptors[i].address);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_WriteResourceDescriptorsEXT(VkDevice device,
                               uint32_t resourceCount,
                               const VkResourceDescriptorInfoEXT *pResources,
                               const VkHostAddressRangeEXT *pDescriptors)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO move out from primary ring? */
   for (uint32_t i = 0; i < resourceCount; i++) {
      VkResult result = vn_call_vkWriteResourceDescriptorMESA(
         dev->primary_ring, device, &pResources[i], pDescriptors[i].size,
         pDescriptors[i].address);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_GetImageOpaqueCaptureDataEXT(VkDevice device,
                                uint32_t imageCount,
                                const VkImage *pImages,
                                VkHostAddressRangeEXT *pDatas)
{
   struct vn_device *dev = vn_device_from_handle(device);
   return vn_call_vkGetImageOpaqueCaptureDataEXT(dev->primary_ring, device,
                                                 imageCount, pImages, pDatas);
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_RegisterCustomBorderColorEXT(
   VkDevice device,
   const VkSamplerCustomBorderColorCreateInfoEXT *pBorderColor,
   VkBool32 requestIndex,
   uint32_t *pIndex)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO manage indexes to make it async */
   return vn_call_vkRegisterCustomBorderColorEXT(
      dev->primary_ring, device, pBorderColor, requestIndex, pIndex);
}

VKAPI_ATTR void VKAPI_CALL
vn_UnregisterCustomBorderColorEXT(VkDevice device, uint32_t index)
{
   struct vn_device *dev = vn_device_from_handle(device);
   vn_async_vkUnregisterCustomBorderColorEXT(dev->primary_ring, device,
                                             index);
}
