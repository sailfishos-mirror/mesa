/*
 * Copyright 2018 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "Resources.h"

#include <stdlib.h>

#include "util/detect_os.h"
#include "util/log.h"

#define GOLDFISH_VK_OBJECT_DEBUG 0

#if GOLDFISH_VK_OBJECT_DEBUG
#define D(fmt, ...) ALOGD("%s: " fmt, __func__, ##__VA_ARGS__);
#else
#ifndef D
#define D(fmt, ...)
#endif
#endif

extern "C" {

#if DETECT_OS_ANDROID
#define SET_HWVULKAN_DISPATCH_MAGIC res->dispatch.magic = HWVULKAN_DISPATCH_MAGIC;
#elif DETECT_OS_LINUX
#define SET_HWVULKAN_DISPATCH_MAGIC res->loaderData.loaderMagic = ICD_LOADER_MAGIC;
#else
#define SET_HWVULKAN_DISPATCH_MAGIC
#endif

#define GOLDFISH_VK_NEW_DISPATCHABLE_FROM_HOST_IMPL(type)                      \
    type new_from_host_##type(type underlying) {                               \
        struct goldfish_##type* res =                                          \
            static_cast<goldfish_##type*>(calloc(1, sizeof(goldfish_##type))); \
        if (!res) {                                                            \
            mesa_loge("FATAL: Failed to alloc " #type " handle");              \
            abort();                                                           \
        }                                                                      \
        SET_HWVULKAN_DISPATCH_MAGIC                                            \
        res->underlying = (uint64_t)underlying;                                \
        return reinterpret_cast<type>(res);                                    \
    }

#define GOLDFISH_VK_NEW_TRIVIAL_NON_DISPATCHABLE_FROM_HOST_IMPL(type)          \
    type new_from_host_##type(type underlying) {                               \
        struct goldfish_##type* res =                                          \
            static_cast<goldfish_##type*>(calloc(1, sizeof(goldfish_##type))); \
        res->underlying = (uint64_t)underlying;                                \
        return reinterpret_cast<type>(res);                                    \
    }

#define GOLDFISH_VK_AS_GOLDFISH_IMPL(type)                    \
    struct goldfish_##type* as_goldfish_##type(type toCast) { \
        return reinterpret_cast<goldfish_##type*>(toCast);    \
    }

#define GOLDFISH_VK_GET_HOST_IMPL(type)                  \
    type get_host_##type(type toUnwrap) {                \
        if (!toUnwrap) return VK_NULL_HANDLE;            \
        auto as_goldfish = as_goldfish_##type(toUnwrap); \
        return (type)(as_goldfish->underlying);          \
    }

#define GOLDFISH_VK_DELETE_GOLDFISH_IMPL(type)   \
    void delete_goldfish_##type(type toDelete) { \
        D("guest %p", toDelete);                 \
        free(as_goldfish_##type(toDelete));      \
    }

#define GOLDFISH_VK_IDENTITY_IMPL(type) \
    type vk_handle_identity_##type(type handle) { return handle; }

#define GOLDFISH_VK_NEW_DISPATCHABLE_FROM_HOST_U64_IMPL(type)                  \
    type new_from_host_u64_##type(uint64_t underlying) {                       \
        struct goldfish_##type* res =                                          \
            static_cast<goldfish_##type*>(calloc(1, sizeof(goldfish_##type))); \
        if (!res) {                                                            \
            mesa_loge("FATAL: Failed to alloc " #type " handle");              \
            abort();                                                           \
        }                                                                      \
        SET_HWVULKAN_DISPATCH_MAGIC                                            \
        res->underlying = underlying;                                          \
        return reinterpret_cast<type>(res);                                    \
    }

#define GOLDFISH_VK_NEW_TRIVIAL_NON_DISPATCHABLE_FROM_HOST_U64_IMPL(type)          \
    type new_from_host_u64_##type(uint64_t underlying) {                           \
        struct goldfish_##type* res =                                              \
            static_cast<goldfish_##type*>(calloc(1, sizeof(goldfish_##type)));     \
        res->underlying = underlying;                                              \
        D("guest %p: host u64: 0x%llx", res, (unsigned long long)res->underlying); \
        return reinterpret_cast<type>(res);                                        \
    }

#define GOLDFISH_VK_GET_HOST_U64_IMPL(type)                                                     \
    uint64_t get_host_u64_##type(type toUnwrap) {                                               \
        if (!toUnwrap) return 0;                                                                \
        auto as_goldfish = as_goldfish_##type(toUnwrap);                                        \
        D("guest %p: host u64: 0x%llx", toUnwrap, (unsigned long long)as_goldfish->underlying); \
        return as_goldfish->underlying;                                                         \
    }

GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_AS_GOLDFISH_IMPL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_IMPL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_IDENTITY_IMPL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_U64_IMPL)
GOLDFISH_VK_LIST_AUTODEFINED_STRUCT_DISPATCHABLE_HANDLE_TYPES(
    GOLDFISH_VK_NEW_DISPATCHABLE_FROM_HOST_IMPL)
GOLDFISH_VK_LIST_AUTODEFINED_STRUCT_DISPATCHABLE_HANDLE_TYPES(
    GOLDFISH_VK_NEW_DISPATCHABLE_FROM_HOST_U64_IMPL)
GOLDFISH_VK_LIST_AUTODEFINED_STRUCT_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_DELETE_GOLDFISH_IMPL)

GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_AS_GOLDFISH_IMPL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_IMPL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_IDENTITY_IMPL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_U64_IMPL)
GOLDFISH_VK_LIST_AUTODEFINED_STRUCT_NON_DISPATCHABLE_HANDLE_TYPES(
    GOLDFISH_VK_NEW_TRIVIAL_NON_DISPATCHABLE_FROM_HOST_IMPL)
GOLDFISH_VK_LIST_AUTODEFINED_STRUCT_NON_DISPATCHABLE_HANDLE_TYPES(
    GOLDFISH_VK_NEW_TRIVIAL_NON_DISPATCHABLE_FROM_HOST_U64_IMPL)
GOLDFISH_VK_LIST_AUTODEFINED_STRUCT_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_DELETE_GOLDFISH_IMPL)
GOLDFISH_VK_DELETE_GOLDFISH_IMPL(VkDescriptorPool)
GOLDFISH_VK_DELETE_GOLDFISH_IMPL(VkDescriptorSet)
GOLDFISH_VK_DELETE_GOLDFISH_IMPL(VkDescriptorSetLayout)

#define GOLDFISH_VK_GFXSTREAM_NON_DISPATCHABLE_IMPL(type, vk_obj_type)         \
    type new_from_host_u64_##type(uint64_t underlying) {                       \
        struct goldfish_##type* res =                                          \
            static_cast<goldfish_##type*>(calloc(1, sizeof(goldfish_##type))); \
        vk_object_base_init(nullptr, &res->vk.base, vk_obj_type);              \
        res->underlying = underlying;                                          \
        return reinterpret_cast<type>(res);                                    \
    }                                                                          \
    type new_from_host_##type(type underlying) {                               \
        return new_from_host_u64_##type((uint64_t)underlying);                 \
    }                                                                          \
    void delete_goldfish_##type(type toDelete) {                               \
        if (!toDelete) return;                                                 \
        auto* res = as_goldfish_##type(toDelete);                              \
        vk_object_base_finish(&res->vk.base);                                  \
        free(res);                                                             \
    }

GOLDFISH_VK_GFXSTREAM_NON_DISPATCHABLE_IMPL(VkBuffer, VK_OBJECT_TYPE_BUFFER)
GOLDFISH_VK_GFXSTREAM_NON_DISPATCHABLE_IMPL(VkFence, VK_OBJECT_TYPE_FENCE)
GOLDFISH_VK_GFXSTREAM_NON_DISPATCHABLE_IMPL(VkSemaphore, VK_OBJECT_TYPE_SEMAPHORE)

VkCommandPool new_from_host_u64_VkCommandPool(uint64_t underlying) {
    struct goldfish_VkCommandPool* res =
        static_cast<goldfish_VkCommandPool*>(calloc(1, sizeof(goldfish_VkCommandPool)));
    vk_object_base_init(nullptr, &res->vk.base, VK_OBJECT_TYPE_COMMAND_POOL);
    list_inithead(&res->vk.command_buffers);
    for (uint32_t i = 0; i < ARRAY_SIZE(res->vk.free_command_buffers); i++) {
        list_inithead(&res->vk.free_command_buffers[i]);
    }
    res->underlying = underlying;
    return reinterpret_cast<VkCommandPool>(res);
}

VkCommandPool new_from_host_VkCommandPool(VkCommandPool underlying) {
    return new_from_host_u64_VkCommandPool((uint64_t)underlying);
}

void delete_goldfish_VkCommandPool(VkCommandPool toDelete) {
    if (!toDelete) return;
    auto* res = as_goldfish_VkCommandPool(toDelete);
    vk_command_pool_finish(&res->vk);
    free(res);
}

VkCommandBuffer new_from_host_u64_VkCommandBuffer(uint64_t underlying) {
    struct goldfish_VkCommandBuffer* res =
        static_cast<goldfish_VkCommandBuffer*>(calloc(1, sizeof(goldfish_VkCommandBuffer)));
    vk_object_base_init(nullptr, &res->vk.base, VK_OBJECT_TYPE_COMMAND_BUFFER);
    list_inithead(&res->vk.pool_link);
    res->underlying = underlying;
    return reinterpret_cast<VkCommandBuffer>(res);
}

VkCommandBuffer new_from_host_VkCommandBuffer(VkCommandBuffer underlying) {
    return new_from_host_u64_VkCommandBuffer((uint64_t)underlying);
}

void delete_goldfish_VkCommandBuffer(VkCommandBuffer toDelete) {
    if (!toDelete) return;
    auto* res = as_goldfish_VkCommandBuffer(toDelete);
    vk_command_buffer_finish(&res->vk);
    free(res);
}

VkQueue new_from_host_u64_VkQueue(uint64_t underlying) {
    struct goldfish_VkQueue* res =
        static_cast<goldfish_VkQueue*>(calloc(1, sizeof(goldfish_VkQueue)));
    vk_object_base_init(nullptr, &res->vk.base, VK_OBJECT_TYPE_QUEUE);
    list_inithead(&res->vk.link);
    res->underlying = underlying;
    return reinterpret_cast<VkQueue>(res);
}

VkQueue new_from_host_VkQueue(VkQueue underlying) {
    return new_from_host_u64_VkQueue((uint64_t)underlying);
}

void delete_goldfish_VkQueue(VkQueue toDelete) {
    if (!toDelete) return;
    auto* res = as_goldfish_VkQueue(toDelete);
    vk_queue_finish(&res->vk);
    free(res);
}

VkDescriptorPool new_from_host_VkDescriptorPool(VkDescriptorPool underlying) {
    struct goldfish_VkDescriptorPool* res =
        static_cast<goldfish_VkDescriptorPool*>(malloc(sizeof(goldfish_VkDescriptorPool)));
    res->underlying = (uint64_t)underlying;
    res->allocInfo = nullptr;
    return reinterpret_cast<VkDescriptorPool>(res);
}

VkDescriptorPool new_from_host_u64_VkDescriptorPool(uint64_t underlying) {
    return new_from_host_VkDescriptorPool((VkDescriptorPool)underlying);
}

VkDescriptorSet new_from_host_VkDescriptorSet(VkDescriptorSet underlying) {
    struct goldfish_VkDescriptorSet* res =
        static_cast<goldfish_VkDescriptorSet*>(malloc(sizeof(goldfish_VkDescriptorSet)));
    res->underlying = (uint64_t)underlying;
    res->reified = nullptr;
    return reinterpret_cast<VkDescriptorSet>(res);
}

VkDescriptorSet new_from_host_u64_VkDescriptorSet(uint64_t underlying) {
    return new_from_host_VkDescriptorSet((VkDescriptorSet)underlying);
}

VkDescriptorSetLayout new_from_host_VkDescriptorSetLayout(VkDescriptorSetLayout underlying) {
    struct goldfish_VkDescriptorSetLayout* res = static_cast<goldfish_VkDescriptorSetLayout*>(
        malloc(sizeof(goldfish_VkDescriptorSetLayout)));
    res->underlying = (uint64_t)underlying;
    res->layoutInfo = nullptr;
    return reinterpret_cast<VkDescriptorSetLayout>(res);
}

VkDescriptorSetLayout new_from_host_u64_VkDescriptorSetLayout(uint64_t underlying) {
    return new_from_host_VkDescriptorSetLayout((VkDescriptorSetLayout)underlying);
}

}  // extern "C"

namespace gfxstream {
namespace vk {

void appendObject(struct gfxstream_vk_object_list** begin, void* val) {
    D("for %p", val);
    struct gfxstream_vk_object_list* o = new gfxstream_vk_object_list;
    o->next = nullptr;
    o->obj = val;
    D("new ptr: %p", o);
    if (!*begin) {
        D("first");
        *begin = o;
        return;
    }

    struct gfxstream_vk_object_list* q = *begin;
    struct gfxstream_vk_object_list* p = q;

    while (q) {
        p = q;
        q = q->next;
    }

    D("set next of %p to %p", p, o);
    p->next = o;
}

void eraseObject(struct gfxstream_vk_object_list** begin, void* val) {
    D("for val %p", val);
    if (!*begin) {
        D("val %p notfound", val);
        return;
    }

    struct gfxstream_vk_object_list* q = *begin;
    struct gfxstream_vk_object_list* p = q;

    while (q) {
        struct gfxstream_vk_object_list* n = q->next;
        if (val == q->obj) {
            D("val %p found, delete", val);
            delete q;
            if (*begin == q) {
                D("val %p set begin to %p:", val, n);
                *begin = n;
            } else {
                D("val %p set pnext to %p:", val, n);
                p->next = n;
            }
            return;
        }
        p = q;
        q = n;
    }

    D("val %p notfound after looping", val);
}

void eraseObjects(struct gfxstream_vk_object_list** begin) {
    struct gfxstream_vk_object_list* q = *begin;
    struct gfxstream_vk_object_list* p = q;

    while (q) {
        p = q;
        q = q->next;
        delete p;
    }

    *begin = nullptr;
}

void forAllObjects(struct gfxstream_vk_object_list* begin, std::function<void(void*)> func) {
    struct gfxstream_vk_object_list* q = begin;
    struct gfxstream_vk_object_list* p = q;

    D("call");
    while (q) {
        D("iter");
        p = q;
        q = q->next;
        func(p->obj);
    }
}

}  // namespace vk
}  // namespace gfxstream
