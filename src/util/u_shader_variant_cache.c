/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "util/u_shader_variant_cache.h"

#include "util/hash_table.h"
#include "util/u_atomic.h"
#include "util/u_memory.h"

#include <assert.h>
#include <string.h>

static struct util_shader_variant *
find_locked(const struct util_shader_variant_cache_options *options,
            struct util_shader_variant_list *list,
            uint32_t hash,
            const void *key,
            size_t key_size)
{
   struct util_shader_variant *v;

   LIST_FOR_EACH_ENTRY(v, &list->variants, link) {
      if (v->key_hash != hash)
         continue;

      bool match = options->equal
         ? options->equal(v->key, key)
         : memcmp(v->key, key, key_size) == 0;

      if (match)
         return v;
   }

   return NULL;
}

static void
destroy_variant(const struct util_shader_variant_cache_options *options,
                struct util_shader_variant *v)
{
   FREE(v->key);
   options->destroy(v);
}

void
util_shader_variant_list_init(struct util_shader_variant_list *list)
{
   simple_mtx_init(&list->lock, mtx_plain);
   list_inithead(&list->variants);
}

void
util_shader_variant_list_destroy(const struct util_shader_variant_cache_options *options,
                                 struct util_shader_variant_list *list)
{
   /* Caller guarantees the CSO is no longer reachable when this runs.
    * No lock needed.
    */
   struct util_shader_variant *v, *next;

   LIST_FOR_EACH_ENTRY_SAFE(v, next, &list->variants, link) {
      list_del(&v->link);
      destroy_variant(options, v);
   }

   simple_mtx_destroy(&list->lock);
}

struct util_shader_variant *
util_shader_variant_get(const struct util_shader_variant_cache_options *options,
                        struct util_shader_variant_list *list,
                        void *cso,
                        const void *key,
                        size_t key_size,
                        bool *was_cache_miss)
{
   assert((options->hash == NULL) == (options->equal == NULL));

   if (was_cache_miss)
      *was_cache_miss = false;

   const uint32_t hash = options->hash
      ? options->hash(key)
      : _mesa_hash_data(key, key_size);

   simple_mtx_lock(&list->lock);
   struct util_shader_variant *v =
      find_locked(options, list, hash, key, key_size);
   simple_mtx_unlock(&list->lock);
   if (v)
      return v;

   /* Allocate the key copy before calling compile so a later OOM
    * never leaves a compiled variant half-initialized.
    */
   void *key_copy = mem_dup(key, key_size);
   if (!key_copy)
      return NULL;

   /* Compile outside the lock. */
   struct util_shader_variant *new_v = options->compile(options->user_data, cso, key);
   if (!new_v) {
      FREE(key_copy);
      return NULL;
   }

   new_v->key_hash = hash;
   new_v->key = key_copy;

   simple_mtx_lock(&list->lock);
   struct util_shader_variant *dup =
      find_locked(options, list, hash, key, key_size);
   if (dup) {
      /* Lost the race. Throw our compile away, return the winner. */
      simple_mtx_unlock(&list->lock);
      destroy_variant(options, new_v);
      return dup;
   }

   list_add(&new_v->link, &list->variants);
   simple_mtx_unlock(&list->lock);

   if (was_cache_miss)
      *was_cache_miss = true;

   return new_v;
}
