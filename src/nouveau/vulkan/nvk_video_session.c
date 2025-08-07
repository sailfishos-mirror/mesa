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

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetPhysicalDeviceVideoCapabilitiesKHR(VkPhysicalDevice physicalDevice,
                                          const VkVideoProfileInfoKHR *pVideoProfile,
                                          VkVideoCapabilitiesKHR *pCapabilities)
{
   pCapabilities->flags = 0;
   pCapabilities->minBitstreamBufferOffsetAlignment = 256;
   pCapabilities->minBitstreamBufferSizeAlignment = 256;
   pCapabilities->pictureAccessGranularity.width = VK_VIDEO_H264_MACROBLOCK_WIDTH;
   pCapabilities->pictureAccessGranularity.height = VK_VIDEO_H264_MACROBLOCK_HEIGHT;
   pCapabilities->minCodedExtent.width = 48;
   pCapabilities->minCodedExtent.height = VK_VIDEO_H264_MACROBLOCK_HEIGHT;
   pCapabilities->maxCodedExtent.width = 4096;
   pCapabilities->maxCodedExtent.height = 4096;

   struct VkVideoDecodeCapabilitiesKHR *dec_caps =
      vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_CAPABILITIES_KHR);
   if (dec_caps)
      dec_caps->flags = VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;

   /* H264 allows different luma and chroma bit depths */
   if (pVideoProfile->lumaBitDepth != pVideoProfile->chromaBitDepth)
      return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

   if (pVideoProfile->chromaSubsampling != VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR)
      return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

   switch (pVideoProfile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      const struct VkVideoDecodeH264ProfileInfoKHR *h264_profile =
         vk_find_struct_const(pVideoProfile->pNext,
                              VIDEO_DECODE_H264_PROFILE_INFO_KHR);
      struct VkVideoDecodeH264CapabilitiesKHR *ext =
         vk_find_struct(pCapabilities->pNext,
                        VIDEO_DECODE_H264_CAPABILITIES_KHR);

      if (h264_profile->stdProfileIdc != STD_VIDEO_H264_PROFILE_IDC_BASELINE &&
          h264_profile->stdProfileIdc != STD_VIDEO_H264_PROFILE_IDC_MAIN &&
          h264_profile->stdProfileIdc != STD_VIDEO_H264_PROFILE_IDC_HIGH)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      if (pVideoProfile->lumaBitDepth != VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      pCapabilities->flags = VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;
      pCapabilities->maxDpbSlots = 17;
      pCapabilities->maxActiveReferencePictures = 16;
      ext->fieldOffsetGranularity.x = 0;
      ext->fieldOffsetGranularity.y = 0;
      ext->maxLevelIdc = STD_VIDEO_H264_LEVEL_IDC_5_2;
      strcpy(pCapabilities->stdHeaderVersion.extensionName,
             VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion =
         VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION;
      break;
   }

   default:
      return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetPhysicalDeviceVideoFormatPropertiesKHR(VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceVideoFormatInfoKHR *pVideoFormatInfo,
   uint32_t *pVideoFormatPropertyCount,
   VkVideoFormatPropertiesKHR *pVideoFormatProperties)
{
   VK_OUTARRAY_MAKE_TYPED(VkVideoFormatPropertiesKHR, out,
                          pVideoFormatProperties, pVideoFormatPropertyCount);

   vk_outarray_append_typed(VkVideoFormatPropertiesKHR, &out, p) {
      p->format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
      p->imageType = VK_IMAGE_TYPE_2D;
      p->imageTiling = VK_IMAGE_TILING_OPTIMAL;
      p->imageUsageFlags = pVideoFormatInfo->imageUsage;
   }

   return vk_outarray_status(&out);
}
