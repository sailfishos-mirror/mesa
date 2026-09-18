/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "util/detect_os.h"

#if DETECT_OS_ANDROID
#include <hardware/hwvulkan.h>
#elif DETECT_OS_LINUX
#include <vulkan/vk_icd.h>
#endif
#include <inttypes.h>
#include <vulkan/vulkan.h>

#include <functional>

#include "VulkanHandles.h"
#include "gfxstream_vk_private.h"

namespace gfxstream {
namespace guest {
class IOStream;
}  // namespace guest
}  // namespace gfxstream

namespace gfxstream {
namespace vk {
class VkEncoder;
struct DescriptorPoolAllocationInfo;
struct ReifiedDescriptorSet;
struct DescriptorSetLayoutInfo;
}  // namespace vk
}  // namespace gfxstream

extern "C" {

// TODO: remove these after fully consolidating to gfxstream_vk_* structs.
#define goldfish_VkBuffer gfxstream_vk_buffer
#define goldfish_VkFence gfxstream_vk_fence
#define goldfish_VkSemaphore gfxstream_vk_semaphore
#define goldfish_VkCommandPool gfxstream_vk_command_pool
#define goldfish_VkCommandBuffer gfxstream_vk_command_buffer
#define goldfish_VkQueue gfxstream_vk_queue
#define goldfish_VkInstance gfxstream_vk_instance
#define goldfish_VkPhysicalDevice gfxstream_vk_physical_device
#define goldfish_VkDevice gfxstream_vk_device

#if DETECT_OS_ANDROID
#define DECLARE_HWVULKAN_DISPATCH hwvulkan_dispatch_t dispatch;
#elif DETECT_OS_LINUX
#define DECLARE_HWVULKAN_DISPATCH VK_LOADER_DATA loaderData;
#else
#define DECLARE_HWVULKAN_DISPATCH
#endif

#define GOLDFISH_VK_DEFINE_TRIVIAL_NON_DISPATCHABLE_HANDLE_STRUCT(type) \
    struct goldfish_##type {                                            \
        uint64_t underlying;                                            \
    };

#define GOLDFISH_VK_NEW_FROM_HOST_DECL(type) type new_from_host_##type(type);

#define GOLDFISH_VK_AS_GOLDFISH_DECL(type) struct goldfish_##type* as_goldfish_##type(type);

#define GOLDFISH_VK_GET_HOST_DECL(type) type get_host_##type(type);

#define GOLDFISH_VK_DELETE_GOLDFISH_DECL(type) void delete_goldfish_##type(type);

#define GOLDFISH_VK_IDENTITY_DECL(type) type vk_handle_identity_##type(type);

#define GOLDFISH_VK_NEW_FROM_HOST_U64_DECL(type) type new_from_host_u64_##type(uint64_t);

#define GOLDFISH_VK_GET_HOST_U64_DECL(type) uint64_t get_host_u64_##type(type);


GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_NEW_FROM_HOST_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_AS_GOLDFISH_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_DELETE_GOLDFISH_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_IDENTITY_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_NEW_FROM_HOST_U64_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_U64_DECL)

GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_NEW_FROM_HOST_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_AS_GOLDFISH_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_DELETE_GOLDFISH_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_IDENTITY_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_NEW_FROM_HOST_U64_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_U64_DECL)
GOLDFISH_VK_LIST_AUTODEFINED_STRUCT_NON_DISPATCHABLE_HANDLE_TYPES(
    GOLDFISH_VK_DEFINE_TRIVIAL_NON_DISPATCHABLE_HANDLE_STRUCT)

struct goldfish_VkDescriptorPool {
    uint64_t underlying;
    gfxstream::vk::DescriptorPoolAllocationInfo* allocInfo;
};

struct goldfish_VkDescriptorSet {
    uint64_t underlying;
    gfxstream::vk::ReifiedDescriptorSet* reified;
};

struct goldfish_VkDescriptorSetLayout {
    uint64_t underlying;
    gfxstream::vk::DescriptorSetLayoutInfo* layoutInfo;
};

}  // extern "C"

namespace gfxstream {
namespace vk {

void appendObject(struct gfxstream_vk_object_list** begin, void* val);
void eraseObject(struct gfxstream_vk_object_list** begin, void* val);
void eraseObjects(struct gfxstream_vk_object_list** begin);
void forAllObjects(struct gfxstream_vk_object_list* begin, std::function<void(void*)> func);

}  // namespace vk
}  // namespace gfxstream
