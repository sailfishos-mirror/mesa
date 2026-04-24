/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include "util/u_shader_variant_cache.h"
#include "util/u_memory.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

struct test_ctx;

struct test_variant {
   struct util_shader_variant base;
   struct test_ctx *tctx;
   int tag;
};

struct test_ctx {
   std::atomic<int> compile_calls{0};
   std::atomic<int> destroy_calls{0};
   std::atomic<int> next_tag{1};
   /* If non-zero, compile returns NULL when compile_calls hits this value. */
   int fail_after = 0;
};

static struct util_shader_variant *
test_compile(void *user_data, UNUSED void *cso, UNUSED const void *key)
{
   struct test_ctx *tctx = (struct test_ctx *)user_data;
   int n = ++tctx->compile_calls;

   if (tctx->fail_after && n >= tctx->fail_after)
      return NULL;

   struct test_variant *v = CALLOC_STRUCT(test_variant);
   if (!v)
      return NULL;

   v->tctx = tctx;
   v->tag = tctx->next_tag.fetch_add(1);

   return &v->base;
}

static void
test_destroy(struct util_shader_variant *variant)
{
   struct test_variant *v = container_of(variant, struct test_variant, base);

   v->tctx->destroy_calls++;
   FREE(v);
}

TEST(u_shader_variant_cache, list_init_destroy_empty)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   EXPECT_TRUE(list_is_empty(&list.variants));

   util_shader_variant_list_destroy(&opts, &list);

   /* An empty list destroyed should not invoke any destroy callbacks. */
   EXPECT_EQ(tctx.destroy_calls.load(), 0);
}

TEST(u_shader_variant_cache, get_compiles_on_miss)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t key = 0x1234;
   struct util_shader_variant *v =
      util_shader_variant_get(&opts, &list, /*cso*/ nullptr,
                              &key, sizeof(key), nullptr);
   ASSERT_NE(v, nullptr);
   EXPECT_EQ(tctx.compile_calls.load(), 1);
   EXPECT_EQ(list_length(&list.variants), 1);

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 1);
}

TEST(u_shader_variant_cache, get_hit_second_lookup)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t key = 0xABCD;
   struct util_shader_variant *a =
      util_shader_variant_get(&opts, &list, nullptr,
                              &key, sizeof(key), nullptr);
   ASSERT_NE(a, nullptr);

   struct util_shader_variant *b =
      util_shader_variant_get(&opts, &list, nullptr,
                              &key, sizeof(key), nullptr);
   EXPECT_EQ(a, b);
   EXPECT_EQ(tctx.compile_calls.load(), 1);
   EXPECT_EQ(list_length(&list.variants), 1);

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 1);
}

TEST(u_shader_variant_cache, was_cache_miss_reporting)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t k1 = 1, k2 = 2;
   bool miss = false;

   /* First lookup compiles. */
   struct util_shader_variant *a =
      util_shader_variant_get(&opts, &list, nullptr,
                              &k1, sizeof(k1), &miss);
   ASSERT_NE(a, nullptr);
   EXPECT_TRUE(miss);

   /* Second lookup of the same key hits. */
   struct util_shader_variant *b =
      util_shader_variant_get(&opts, &list, nullptr,
                              &k1, sizeof(k1), &miss);
   EXPECT_EQ(b, a);
   EXPECT_FALSE(miss);

   /* A different key misses again. */
   struct util_shader_variant *c =
      util_shader_variant_get(&opts, &list, nullptr,
                              &k2, sizeof(k2), &miss);
   ASSERT_NE(c, nullptr);
   EXPECT_NE(c, a);
   EXPECT_TRUE(miss);

   util_shader_variant_list_destroy(&opts, &list);
}

TEST(u_shader_variant_cache, multiple_variants_one_list)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t keys[3] = { 1, 2, 3 };
   struct util_shader_variant *vs[3];

   for (int i = 0; i < 3; ++i) {
      vs[i] = util_shader_variant_get(&opts, &list, nullptr,
                                      &keys[i], sizeof(keys[i]), nullptr);
      ASSERT_NE(vs[i], nullptr);
   }

   EXPECT_EQ(tctx.compile_calls.load(), 3);
   EXPECT_EQ(list_length(&list.variants), 3);

   EXPECT_NE(vs[0], vs[1]);
   EXPECT_NE(vs[1], vs[2]);
   EXPECT_NE(vs[0], vs[2]);

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 3);
}

TEST(u_shader_variant_cache, compile_null_is_surfaced)
{
   struct test_ctx tctx;
   tctx.fail_after = 1;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t k = 99;
   struct util_shader_variant *v =
      util_shader_variant_get(&opts, &list, nullptr, &k, sizeof(k), nullptr);
   EXPECT_EQ(v, nullptr);
   EXPECT_EQ(tctx.compile_calls.load(), 1);
   EXPECT_EQ(tctx.destroy_calls.load(), 0);
   EXPECT_EQ(list_length(&list.variants), 0);

   util_shader_variant_list_destroy(&opts, &list);
}

TEST(u_shader_variant_cache, concurrent_get)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   constexpr int num_threads = 8;
   constexpr int num_iters   = 20000;
   constexpr int num_keys    = 32;

   std::atomic<int> misses{0};

   auto worker = [&]() {
      for (int i = 0; i < num_iters; ++i) {
         uint32_t k = (uint32_t)(i % num_keys);
         bool miss = false;
         struct util_shader_variant *v =
            util_shader_variant_get(&opts, &list, nullptr,
                                    &k, sizeof(k), &miss);
         ASSERT_NE(v, nullptr);
         if (miss)
            misses++;
      }
   };

   std::vector<std::thread> threads;

   for (int i = 0; i < num_threads; ++i)
      threads.emplace_back(worker);

   for (auto &t : threads)
      t.join();

   EXPECT_EQ(list_length(&list.variants), num_keys);

   /* Only the publish winner of each key reports a miss; peers that
    * compiled but lost the race (dedup-on-publish) report a hit.
    */
   EXPECT_EQ(misses.load(), num_keys);

   /* Race-on-publish loses a few compiles to dedup. The losing
    * compiles get destroyed inline, so destroy_calls so far equals
    * (compiles - num_keys).
    */
   int compiles = tctx.compile_calls.load();
   int destroys_so_far = tctx.destroy_calls.load();
   EXPECT_EQ(destroys_so_far, compiles - num_keys);

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), compiles);
}
