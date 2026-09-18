/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "ResourceTracker.h"
#include "VkEncoder.h"
#include "gfxstream_vk_private.h"
#include "util/perf/cpu_trace.h"

VkResult gfxstream_vk_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo,
                                        const VkAllocationCallbacks* pAllocator,
                                        VkCommandPool* pCommandPool) {
    MESA_TRACE_SCOPE("vkCreateCommandPool");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
    VkResult result = vkEnc->vkCreateCommandPool(gfxstream_device->internal_object, pCreateInfo,
                                                 pAllocator, pCommandPool, true /* do lock */);
    if (VK_SUCCESS == result) {
        VK_FROM_HANDLE(gfxstream_vk_command_pool, gfxstream_pCommandPool, *pCommandPool);
        result = vk_command_pool_init(&gfxstream_device->vk, &gfxstream_pCommandPool->vk,
                                      pCreateInfo, pAllocator);
    }
    return result;
}

void gfxstream_vk_DestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                                     const VkAllocationCallbacks* pAllocator) {
    MESA_TRACE_SCOPE("vkDestroyCommandPool");
    if (VK_NULL_HANDLE == commandPool) {
        return;
    }
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
    vkEnc->vkDestroyCommandPool(gfxstream_device->internal_object, commandPool, pAllocator,
                                true /* do lock */);
}

VkResult gfxstream_vk_ResetCommandPool(VkDevice device, VkCommandPool commandPool,
                                       VkCommandPoolResetFlags flags) {
    MESA_TRACE_SCOPE("vkResetCommandPool");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    VkResult vkResetCommandPool_VkResult_return = (VkResult)0;
    {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        vkResetCommandPool_VkResult_return = vkEnc->vkResetCommandPool(
            gfxstream_device->internal_object, commandPool, flags, true /* do lock */);
        if (vkResetCommandPool_VkResult_return == VK_SUCCESS) {
            gfxstream::vk::ResourceTracker::get()->resetCommandPoolStagingInfo(commandPool);
        }
    }
    return vkResetCommandPool_VkResult_return;
}


VkResult gfxstream_vk_AllocateCommandBuffers(VkDevice device,
                                             const VkCommandBufferAllocateInfo* pAllocateInfo,
                                             VkCommandBuffer* pCommandBuffers) {
    MESA_TRACE_SCOPE("vkAllocateCommandBuffers");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    VK_FROM_HANDLE(gfxstream_vk_command_pool, gfxstream_commandPool, pAllocateInfo->commandPool);
    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
        pCommandBuffers[i] = VK_NULL_HANDLE;
    }

    auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
    auto resources = gfxstream::vk::ResourceTracker::get();
    VkResult result = resources->on_vkAllocateCommandBuffers(
        vkEnc, VK_SUCCESS, gfxstream_device->internal_object, pAllocateInfo, pCommandBuffers);
    if (result == VK_SUCCESS) {
        resources->addToCommandPool(pAllocateInfo->commandPool, pAllocateInfo->commandBufferCount,
                                    pCommandBuffers);
        for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
            VK_FROM_HANDLE(gfxstream_vk_command_buffer, gfxstream_commandBuffer,
                           pCommandBuffers[i]);
            result =
                vk_command_buffer_init(&gfxstream_commandPool->vk, &gfxstream_commandBuffer->vk,
                                       nullptr, pAllocateInfo->level);
            if (result != VK_SUCCESS) {
                break;
            }
        }
    }
    return result;
}

void gfxstream_vk_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                     uint32_t commandBufferCount,
                                     const VkCommandBuffer* pCommandBuffers) {
    MESA_TRACE_SCOPE("vkFreeCommandBuffers");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    std::vector<VkCommandBuffer> validCommandBuffers;
    validCommandBuffers.reserve(commandBufferCount);
    for (uint32_t i = 0; i < commandBufferCount; i++) {
        if (pCommandBuffers[i] != VK_NULL_HANDLE) {
            validCommandBuffers.push_back(pCommandBuffers[i]);
        }
    }
    auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
    vkEnc->vkFreeCommandBuffers(gfxstream_device->internal_object, commandPool,
                                validCommandBuffers.size(), validCommandBuffers.data(),
                                true /* do lock */);
}

void gfxstream_vk_CmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                                               uint32_t firstCounterBuffer,
                                               uint32_t counterBufferCount,
                                               const VkBuffer* pCounterBuffers,
                                               const VkDeviceSize* pCounterBufferOffsets) {
    MESA_TRACE_SCOPE("vkCmdBeginTransformFeedbackEXT");
    auto vkEnc = gfxstream::vk::ResourceTracker::getCommandBufferEncoder(commandBuffer);
    vkEnc->vkCmdBeginTransformFeedbackEXT(commandBuffer, firstCounterBuffer, counterBufferCount,
                                          pCounterBuffers, pCounterBufferOffsets,
                                          true /* do lock */);
}
