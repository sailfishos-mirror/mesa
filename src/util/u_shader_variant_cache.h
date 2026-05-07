/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

/*
 * Generic per-shader cache of compiled shader variants.
 *
 * A "variant" is a compiled specialization of a shader, keyed on
 * runtime spec state (e.g. bound vertex formats, rasterizer clip flags).
 * Keys are opaque bytes; variants are opaque pointers; the compile step
 * is a driver callback.
 *
 * Embedding:
 *  - util_shader_variant_cache_options: one per cache scope, embedded by
 *    the caller at whatever scope should share variants. The list always
 *    stays per shader.
 *  - util_shader_variant_list: one per shader that holds variants.
 *  - Driver variants embed struct util_shader_variant and map back to
 *    the containing struct with container_of().
 *
 * Lifetime: variants are owned by the list. There is no per-variant
 * refcount. util_shader_variant_list_destroy fires the destroy callback
 * for every variant in the list; the caller must guarantee no other
 * thread can reach the list at that point.
 *
 * Custom key hash and equality: drivers with a hand-rolled fast-path
 * comparison can supply a hash + equal callback pair in the options. The
 * util uses them in place of _mesa_hash_data and memcmp. Both must be set
 * together or both NULL — the lookup path asserts otherwise, since a
 * mismatched hash would drop matches the custom equality accepts.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/list.h"
#include "util/simple_mtx.h"

#ifdef __cplusplus
extern "C" {
#endif

struct util_shader_variant {
   struct list_head link;
   /* internal: written by the cache, do not touch from drivers */
   uint32_t key_hash;
   void *key;
};

struct util_shader_variant_list {
   struct list_head variants;
   /* internal */
   simple_mtx_t lock;
};

typedef struct util_shader_variant *
(*util_shader_variant_compile_cb)(void *user_data,
                                  void *cso,
                                  const void *key);

typedef void
(*util_shader_variant_destroy_cb)(struct util_shader_variant *variant);

typedef uint32_t (*util_shader_variant_hash_cb)(const void *key);

typedef bool (*util_shader_variant_equal_cb)(const void *a, const void *b);

struct util_shader_variant_cache_options {
   util_shader_variant_compile_cb compile;   /* required */
   util_shader_variant_destroy_cb destroy;   /* required */
   util_shader_variant_hash_cb hash;
   util_shader_variant_equal_cb equal;
   void *user_data;
};

void
util_shader_variant_list_init(struct util_shader_variant_list *list);

void
util_shader_variant_list_destroy(const struct util_shader_variant_cache_options *options,
                                 struct util_shader_variant_list *list);

/* Look up or compile a variant. If was_cache_miss is non-NULL, it is
 * set to true if this call compiled a fresh variant and false if an
 * existing variant was found (or returned via dedup-on-publish after a
 * concurrent peer compiled the same key).
 */
struct util_shader_variant *
util_shader_variant_get(const struct util_shader_variant_cache_options *options,
                        struct util_shader_variant_list *list,
                        void *cso,
                        const void *key,
                        size_t key_size,
                        bool *was_cache_miss);

#ifdef __cplusplus
}
#endif
