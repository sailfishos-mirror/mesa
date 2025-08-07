/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_VIDEO_SESSION_H
#define NVK_VIDEO_SESSION_H 1

#include "nvk_private.h"
#include "nvk_device_memory.h"

#include "vulkan/runtime/vk_video.h"

struct nvk_vid_mem {
   uint64_t size_B;
   uint32_t align_B;

   struct nvk_device_memory *mem;
   VkDeviceSize offset;
   VkDeviceSize size;
};

struct nvk_video_session {
   struct vk_video_session vk;

   struct nvk_vid_mem mems[3];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_video_session, vk.base, VkVideoSessionKHR,
                               VK_OBJECT_TYPE_VIDEO_SESSION_KHR);

#endif /* NVK_VIDEO_SESSION_H */
