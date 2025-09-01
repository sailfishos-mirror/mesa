/*
 * Copyright © 2021 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_AUTOTUNE_H
#define TU_AUTOTUNE_H

#include "tu_common.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "tu_cs.h"
#include "tu_suballoc.h"

/* Autotune allows for us to tune rendering parameters (such as GMEM vs SYSMEM, tile size divisor, etc.) based on
 * dynamic analysis of the rendering workload via on-GPU profiling. This lets us make much better decisions than static
 * analysis, since we can adapt to the actual workload rather than relying on heuristics.
 */
struct tu_autotune {
 private:
   bool enabled = true;
   struct tu_device *device;

   /** Configuration **/

   enum class algorithm : uint8_t;
   enum class mod_flag : uint8_t;
   enum class metric_flag : uint8_t;
   /* Container for all autotune configuration options. */
   struct PACKED config_t;
   union PACKED packed_config_t;

   /* Allows for thread-safe access to the configurations. */
   struct atomic_config_t {
    private:
      std::atomic<uint32_t> config_bits = 0;

    public:
      atomic_config_t(config_t initial_config);

      config_t load() const;

      bool compare_and_store(config_t expected, config_t updated);
   } active_config;

   config_t get_env_config();

   /** Global Fence and Internal CS Management **/

   /* BO suballocator for reducing BO management for small GMEM/SYSMEM autotune result buffers.
    * Synchronized by suballoc_mutex.
    */
   struct tu_suballocator suballoc;
   std::mutex suballoc_mutex;

   /* The next value to assign to tu6_global::autotune_fence, this is incremented during on_submit. */
   uint32_t next_fence = 1;

   /* A wrapper around a CS which sets the global autotune fence to a certain fence value, this allows for ergonomically
    * managing the lifetime of the CS including recycling it after the fence value has been reached.
    */
   struct submission_entry {
    private:
      uint32_t fence;
      struct tu_cs fence_cs;

    public:
      explicit submission_entry(tu_device *device);

      ~submission_entry();

      /* Disable move/copy, since this holds stable pointers to the fence_cs. */
      submission_entry(const submission_entry &) = delete;
      submission_entry &operator=(const submission_entry &) = delete;
      submission_entry(submission_entry &&) = delete;
      submission_entry &operator=(submission_entry &&) = delete;

      /* The current state of the submission entry, this is used to track whether the CS is available for reuse, pending
       * GPU completion or currently being processed.
       */
      bool is_active() const;

      /* If the CS is free, returns the CS which will write out the specified fence value. Otherwise, returns nullptr. */
      struct tu_cs *try_get_cs(uint32_t new_fence);
   };

   /* Unified pool for submission CSes.
    * Note: This is a deque rather than a vector due to the lack of move semantics in the submission_entry.
    */
   std::deque<submission_entry> submission_entries;

   /* Returns a CS which will write out the specified fence value to the global BO's autotune fence. */
   struct tu_cs *get_cs_for_fence(uint32_t fence);

   /** RP Entry Management **/

   struct rp_gpu_data;
   struct tile_gpu_data;
   struct rp_entry;

   /* A wrapper over all entries associated with a single command buffer. */
   struct rp_entry_batch {
      bool active;    /* If the entry is ready to be processed, i.e. the entry is submitted to the GPU queue and has a
                         valid fence. */
      uint32_t fence; /* The fence value which is used to signal the completion of the CB submission. This is used to
                         determine when the entries can be processed. */
      std::vector<std::unique_ptr<rp_entry>> entries;

      rp_entry_batch();

      /* Disable the copy/move to avoid performance hazards. */
      rp_entry_batch(const rp_entry_batch &) = delete;
      rp_entry_batch &operator=(const rp_entry_batch &) = delete;
      rp_entry_batch(rp_entry_batch &&) = delete;
      rp_entry_batch &operator=(rp_entry_batch &&) = delete;

      void assign_fence(uint32_t new_fence);

      void mark_inactive();
   };

   /* A deque of entry batches that are strongly ordered by the fence value that was written by the GPU, for efficient
    * iteration and to ensure that we process the entries in the same order they were submitted.
    */
   std::deque<std::shared_ptr<rp_entry_batch>> active_batches;

   /* Handles processing of entry batches that are pending to be processed.
    *
    * Note: This must be called regularly to process the entries that have been written by the GPU. We currently do this
    *       in the on_submit() method, which is called on every submit of a command buffer.
    */
   void process_entries();

   /** Renderpass State Tracking **/

   struct rp_history;
   struct rp_history_handle;

   /* A strongly typed key which generates a hash to uniquely identify a renderpass instance. This hash is expected to
    * be stable across runs, so it can be used to identify the same renderpass instance consistently.
    *
    * Note: We can potentially include the vector of data we extract from the parameters to generate the hash into
    *       rp_key, which would lead to true value-based equality rather than just hash-based equality which has a cost
    *       but avoids hash collisions causing issues.
    */
   struct rp_key {
      uint64_t hash;

      rp_key(const struct tu_render_pass *pass,
             const struct tu_framebuffer *framebuffer,
             const struct tu_cmd_buffer *cmd);

      /* Further salt the hash to distinguish between multiple instances of the same RP within a single command buffer. */
      rp_key(const rp_key &key, uint32_t duplicates);

      /* Equality operator, used in unordered_map. */
      constexpr bool operator==(const rp_key &other) const noexcept
      {
         return hash == other.hash;
      }
   };

   /* A thin wrapper to satisfy C++'s Hash named requirement for rp_key.
    *
    * Note: This should *NEVER* be used to calculate the hash itself as it would lead to the hash being calculated
    *       multiple times, rather than being calculated once and reused when there's multiple successive lookups like
    *       with find_or_create_rp_history() and providing the hash to the rp_history constructor.
    */
   struct rp_hash {
      constexpr size_t operator()(const rp_key &key) const noexcept
      {
         /* Note: This will throw away the upper 32-bits on 32-bit architectures. */
         return static_cast<size_t>(key.hash);
      }
   };

   /* A map between the hash of an RP and the historical state of the RP. Synchronized by rp_mutex. */
   using rp_histories_t = std::unordered_map<rp_key, rp_history, rp_hash>;
   rp_histories_t rp_histories;
   std::shared_mutex rp_mutex;
   uint64_t last_reap_ts = 0;

   /* Note: These will internally lock rp_mutex internally, no need to lock it. */
   rp_history_handle find_rp_history(const rp_key &key);
   rp_history_handle find_or_create_rp_history(const rp_key &key);
   void reap_old_rp_histories();

 public:
   tu_autotune(struct tu_device *device, VkResult &result);

   ~tu_autotune();

   /* Opaque pointer to internal structure with RP context that needs to be preserved across begin/end calls. */
   using rp_ctx_t = rp_entry *;

   /* An internal structure that needs to be held by tu_cmd_buffer to track the state of the autotuner for a given CB.
    *
    * Note: tu_cmd_buffer is only responsible for the lifetime of this object, all the access to the context state is
    *       done through tu_autotune.
    */
   struct cmd_buf_ctx {
    private:
      /* A batch of all entries from RPs within this CB. */
      std::shared_ptr<rp_entry_batch> batch;

      /* Creates a new RP entry attached to this CB. */
      rp_entry *
      attach_rp_entry(struct tu_device *device, rp_history_handle &&history, config_t config, uint32_t draw_count);

      rp_entry *find_rp_entry(const rp_key &key);

      friend struct tu_autotune;

    public:
      cmd_buf_ctx();
      ~cmd_buf_ctx();

      /* Resets the internal context, should be called when tu_cmd_buffer state has been reset. */
      void reset();
   };

   enum class render_mode {
      SYSMEM,
      GMEM,
   };

   render_mode get_optimal_mode(struct tu_cmd_buffer *cmd_buffer, rp_ctx_t *rp_ctx);

   void begin_renderpass(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx, bool sysmem);

   void end_renderpass(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx);

   /* The submit-time hook for autotuner, this may return a CS (can be NULL) which must be amended for autotuner
    * tracking to function correctly.
    *
    * Note: This must be called from a single-threaded context. There should never be multiple threads calling this
    *       function at the same time.
    */
   struct tu_cs *on_submit(struct tu_cmd_buffer **cmd_buffers, uint32_t cmd_buffer_count);
};

#endif /* TU_AUTOTUNE_H */