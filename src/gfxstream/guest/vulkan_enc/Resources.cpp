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

#define GOLDFISH_VK_AS_GOLDFISH_IMPL(type)                    \
    struct goldfish_##type* as_goldfish_##type(type toCast) { \
        return reinterpret_cast<goldfish_##type*>(toCast);    \
    }

#define GOLDFISH_VK_GET_HOST_IMPL(type)                   \
    type get_host_##type(type toUnwrap) {                 \
        if (!toUnwrap) return VK_NULL_HANDLE;             \
        auto* as_goldfish = as_goldfish_##type(toUnwrap); \
        return (type)(as_goldfish->underlying);           \
    }

#define GOLDFISH_VK_GET_HOST_U64_IMPL(type)                                                     \
    uint64_t get_host_u64_##type(type toUnwrap) {                                               \
        if (!toUnwrap) return 0;                                                                \
        auto* as_goldfish = as_goldfish_##type(toUnwrap);                                       \
        D("guest %p: host u64: 0x%llx", toUnwrap, (unsigned long long)as_goldfish->underlying); \
        return as_goldfish->underlying;                                                         \
    }

#define GOLDFISH_VK_IDENTITY_IMPL(type) \
    type vk_handle_identity_##type(type handle) { return handle; }

GOLDFISH_VK_LIST_HANDLE_TYPES(GOLDFISH_VK_AS_GOLDFISH_IMPL)
GOLDFISH_VK_LIST_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_IMPL)
GOLDFISH_VK_LIST_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_U64_IMPL)
GOLDFISH_VK_LIST_HANDLE_TYPES(GOLDFISH_VK_IDENTITY_IMPL)

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
