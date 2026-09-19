/*
 * Copyright © 2023 Google Inc.
 *
 * derived from panvk_private.h driver which is:
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef GFXSTREAM_VK_PRIVATE_H
#define GFXSTREAM_VK_PRIVATE_H

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>

#include <vector>

#include "gfxstream_vk_entrypoints.h"
#include "vk_alloc.h"
#include "vk_buffer.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"
#include "vk_device.h"
#include "vk_extensions.h"
#include "vk_fence.h"
#include "vk_image.h"
#include "vk_instance.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_physical_device.h"
#include "vk_queue.h"
#include "vk_semaphore.h"
#include "vulkan/wsi/wsi_common.h"

#define GFXSTREAM_DEFAULT_ALIGN 8

namespace gfxstream {
namespace guest {
class IOStream;
}  // namespace guest
namespace vk {
class VkEncoder;
struct DescriptorPoolAllocationInfo;
struct ReifiedDescriptorSet;
struct DescriptorSetLayoutInfo;
}  // namespace vk
}  // namespace gfxstream

#define GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_type, vk_type)             \
    extern "C" vk_type new_from_host_u64_##vk_type(uint64_t underlying); \
    extern "C" vk_type new_from_host_##vk_type(vk_type underlying);      \
    extern "C" void delete_goldfish_##vk_type(vk_type toDelete);

struct gfxstream_vk_instance {
    struct vk_instance vk;
    uint32_t api_version;
    bool init_failed;

    // The untyped host handle.
    uint64_t underlying;
};
VK_DEFINE_HANDLE_CASTS(gfxstream_vk_instance, vk.base, VkInstance, VK_OBJECT_TYPE_INSTANCE)
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_instance, VkInstance)

struct gfxstream_vk_physical_device {
    struct vk_physical_device vk;

    struct wsi_device wsi_device;
    const struct vk_sync_type* sync_types[2];
    struct gfxstream_vk_instance* instance;
    bool doImageDrmFormatModifierEmulation;

    // The untyped host handle.
    uint64_t underlying;
};
VK_DEFINE_HANDLE_CASTS(gfxstream_vk_physical_device, vk.base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_physical_device, VkPhysicalDevice)

struct gfxstream_vk_device {
    struct vk_device vk;

    struct vk_device_dispatch_table cmd_dispatch;
    struct gfxstream_vk_physical_device* physical_device;

    /* unique queue family indices in which to create the device queues */
    uint32_t* queue_families;
    uint32_t queue_family_count;

    // The untyped host handle.
    uint64_t underlying;
};
VK_DEFINE_HANDLE_CASTS(gfxstream_vk_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_device, VkDevice)

struct gfxstream_vk_queue {
    struct vk_queue vk;
    struct gfxstream_vk_device* device;

    // The untyped host handle.
    uint64_t underlying;
    gfxstream::vk::VkEncoder* lastUsedEncoder;
    uint32_t sequenceNumber;
};
VK_DEFINE_HANDLE_CASTS(gfxstream_vk_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_queue, VkQueue)

struct gfxstream_vk_buffer {
    struct vk_buffer vk;

    // The untyped host handle.
    uint64_t underlying;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(gfxstream_vk_buffer, vk.base, VkBuffer, VK_OBJECT_TYPE_BUFFER)
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_buffer, VkBuffer)

struct gfxstream_vk_object_list {
    void* obj;
    struct gfxstream_vk_object_list* next;
};

struct gfxstream_vk_command_pool {
    struct vk_command_pool vk;

    // The untyped host handle.
    uint64_t underlying;

    // The command buffers allocated from this pool.
    struct gfxstream_vk_object_list* subObjects;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(gfxstream_vk_command_pool, vk.base, VkCommandPool,
                               VK_OBJECT_TYPE_COMMAND_POOL)
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_command_pool, VkCommandPool)

struct gfxstream_vk_command_buffer {
    struct vk_command_buffer vk;

    // The untyped host handle.
    uint64_t underlying;

    gfxstream::vk::VkEncoder* lastUsedEncoder;
    uint32_t sequenceNumber;
    gfxstream::vk::VkEncoder* privateEncoder;
    gfxstream::guest::IOStream* privateStream;
    uint32_t flags;
    struct gfxstream_vk_object_list* poolObjects;
    struct gfxstream_vk_object_list* subObjects;
    struct gfxstream_vk_object_list* superObjects;
    void* userPtr;
    bool isSecondary;
    VkDevice device;
};
VK_DEFINE_HANDLE_CASTS(gfxstream_vk_command_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_command_buffer, VkCommandBuffer)

struct gfxstream_vk_fence {
    struct vk_fence vk;

    // The untyped host handle.
    uint64_t underlying;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(gfxstream_vk_fence, vk.base, VkFence, VK_OBJECT_TYPE_FENCE)
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_fence, VkFence)

struct gfxstream_vk_semaphore {
    struct vk_semaphore vk;

    // The untyped host handle.
    uint64_t underlying;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(gfxstream_vk_semaphore, vk.base, VkSemaphore,
                               VK_OBJECT_TYPE_SEMAPHORE)
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_semaphore, VkSemaphore)

struct gfxstream_vk_base_object {
    struct vk_object_base base;

    // The untyped host handle.
    uint64_t underlying;
};

struct gfxstream_vk_descriptor_pool : public gfxstream_vk_base_object {
    gfxstream::vk::DescriptorPoolAllocationInfo* allocInfo;
};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_descriptor_pool, VkDescriptorPool)

struct gfxstream_vk_descriptor_set : public gfxstream_vk_base_object {
    gfxstream::vk::ReifiedDescriptorSet* reified;
};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_descriptor_set, VkDescriptorSet)

struct gfxstream_vk_descriptor_set_layout : public gfxstream_vk_base_object {
    gfxstream::vk::DescriptorSetLayoutInfo* layoutInfo;
};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_descriptor_set_layout, VkDescriptorSetLayout)

struct gfxstream_vk_device_memory : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_device_memory, VkDeviceMemory)

struct gfxstream_vk_image : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_image, VkImage)

struct gfxstream_vk_descriptor_update_template : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_descriptor_update_template, VkDescriptorUpdateTemplate)

struct gfxstream_vk_sampler : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_sampler, VkSampler)

struct gfxstream_vk_private_data_slot : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_private_data_slot, VkPrivateDataSlot)

#ifdef VK_USE_PLATFORM_FUCHSIA
struct gfxstream_vk_buffer_collection_fuchsia : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_buffer_collection_fuchsia, VkBufferCollectionFUCHSIA)
#endif

struct gfxstream_vk_buffer_view : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_buffer_view, VkBufferView)

struct gfxstream_vk_image_view : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_image_view, VkImageView)

struct gfxstream_vk_shader_module : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_shader_module, VkShaderModule)

struct gfxstream_vk_pipeline : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_pipeline, VkPipeline)

struct gfxstream_vk_pipeline_cache : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_pipeline_cache, VkPipelineCache)

struct gfxstream_vk_pipeline_layout : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_pipeline_layout, VkPipelineLayout)

struct gfxstream_vk_render_pass : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_render_pass, VkRenderPass)

struct gfxstream_vk_framebuffer : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_framebuffer, VkFramebuffer)

struct gfxstream_vk_event : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_event, VkEvent)

struct gfxstream_vk_query_pool : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_query_pool, VkQueryPool)

struct gfxstream_vk_sampler_ycbcr_conversion : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_sampler_ycbcr_conversion, VkSamplerYcbcrConversion)

struct gfxstream_vk_surface_khr : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_surface_khr, VkSurfaceKHR)

struct gfxstream_vk_swapchain_khr : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_swapchain_khr, VkSwapchainKHR)

struct gfxstream_vk_display_khr : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_display_khr, VkDisplayKHR)

struct gfxstream_vk_display_mode_khr : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_display_mode_khr, VkDisplayModeKHR)

struct gfxstream_vk_validation_cache_ext : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_validation_cache_ext, VkValidationCacheEXT)

struct gfxstream_vk_debug_report_callback_ext : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_debug_report_callback_ext, VkDebugReportCallbackEXT)

struct gfxstream_vk_debug_utils_messenger_ext : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_debug_utils_messenger_ext, VkDebugUtilsMessengerEXT)

struct gfxstream_vk_micromap_ext : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_micromap_ext, VkMicromapEXT)

#ifdef VK_NVX_binary_import
struct gfxstream_vk_cu_module_nvx : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_cu_module_nvx, VkCuModuleNVX)

struct gfxstream_vk_cu_function_nvx : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_cu_function_nvx, VkCuFunctionNVX)
#endif

#ifdef VK_NVX_device_generated_commands
struct gfxstream_vk_object_table_nvx : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_object_table_nvx, VkObjectTableNVX)

struct gfxstream_vk_indirect_commands_layout_nvx : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_indirect_commands_layout_nvx, VkIndirectCommandsLayoutNVX)
#endif

#ifdef VK_NV_device_generated_commands
struct gfxstream_vk_indirect_commands_layout_nv : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_indirect_commands_layout_nv, VkIndirectCommandsLayoutNV)
#endif

#ifdef VK_NV_ray_tracing
struct gfxstream_vk_acceleration_structure_nv : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_acceleration_structure_nv, VkAccelerationStructureNV)
#endif

#ifdef VK_KHR_acceleration_structure
struct gfxstream_vk_acceleration_structure_khr : public gfxstream_vk_base_object {};
GFXSTREAM_DECLARE_VK_OBJECT(gfxstream_vk_acceleration_structure_khr, VkAccelerationStructureKHR)
#endif

VkResult gfxstream_vk_wsi_init(struct gfxstream_vk_physical_device* physical_device);

void gfxstream_vk_wsi_finish(struct gfxstream_vk_physical_device* physical_device);

std::vector<VkSemaphore> FilterNoopSemaphores(const VkSemaphore* pSemaphores,
                                              uint32_t semaphoreCount);

std::vector<VkFence> FilterNoopFences(const VkFence* pFences, uint32_t fenceCount);

std::vector<VkSemaphoreSubmitInfo> FilterNoopSemaphoreSubmitInfos(
    const VkSemaphoreSubmitInfo* pSemaphoreSubmitInfos, uint32_t semaphoreSubmitInfoCount);

float linearChannelToSRGB(float cl);
float srgbFormatNeedsConversionForClearColor(const VkFormat& format);

#endif /* GFXSTREAM_VK_PRIVATE_H */
