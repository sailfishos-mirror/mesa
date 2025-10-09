/*
 * Copyright © 2021 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "tu_autotune.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include "util/rand_xor.h"

#define XXH_INLINE_ALL
#include "util/xxhash.h"

#include "tu_cmd_buffer.h"
#include "tu_cs.h"
#include "tu_device.h"
#include "tu_image.h"
#include "tu_pass.h"

/** Compile-time debug options **/

#define TU_AUTOTUNE_DEBUG_LOG_BASE      0
#define TU_AUTOTUNE_DEBUG_LOG_BANDWIDTH 0

#if TU_AUTOTUNE_DEBUG_LOG_BASE
#define at_log_base(fmt, ...)         mesa_logi("autotune: " fmt, ##__VA_ARGS__)
#define at_log_base_h(fmt, hash, ...) mesa_logi("autotune %016" PRIx64 ": " fmt, hash, ##__VA_ARGS__)
#else
#define at_log_base(fmt, ...)
#define at_log_base_h(fmt, hash, ...)
#endif

#if TU_AUTOTUNE_DEBUG_LOG_BANDWIDTH
#define at_log_bandwidth_h(fmt, hash, ...) mesa_logi("autotune-bw %016" PRIx64 ": " fmt, hash, ##__VA_ARGS__)
#else
#define at_log_bandwidth_h(fmt, hash, ...)
#endif

/* Process any pending entries on autotuner finish, could be used to gather data from traces. */
#define TU_AUTOTUNE_FLUSH_AT_FINISH 0

/** Global constants and helpers **/

/* GPU always-on timer constants */
constexpr uint64_t ALWAYS_ON_FREQUENCY_HZ = 19'200'000;
constexpr double GPU_TICKS_PER_US = ALWAYS_ON_FREQUENCY_HZ / 1'000'000.0;

constexpr uint64_t
ticks_to_us(uint64_t ticks)
{
   return ticks / GPU_TICKS_PER_US;
}

constexpr bool
fence_before(uint32_t a, uint32_t b)
{
   /* Essentially a < b, but handles wrapped values. */
   return (int32_t) (a - b) < 0;
}

constexpr const char *
render_mode_str(tu_autotune::render_mode mode)
{
   switch (mode) {
   case tu_autotune::render_mode::SYSMEM:
      return "SYSMEM";
   case tu_autotune::render_mode::GMEM:
      return "GMEM";
   default:
      return "UNKNOWN";
   }
}

/** Configuration **/

enum class tu_autotune::algorithm : uint8_t {
   BANDWIDTH = 0,    /* Uses estimated BW for determining rendering mode. */

   DEFAULT = BANDWIDTH, /* Default algorithm, used if no other is specified. */
};

/* Modifier flags, these modify the behavior of the autotuner in a user-defined way. */
enum class tu_autotune::mod_flag : uint8_t {
   BIG_GMEM = BIT(1),          /* All RPs with >= 10 draws use GMEM. */
   SMALL_SYSMEM = BIT(2),      /* All RPs with <= 5 draws use SYSMEM. */
};

/* Metric flags, for internal tracking of enabled metrics. */
enum class tu_autotune::metric_flag : uint8_t {
   SAMPLES = BIT(1), /* Enable tracking samples passed metric. */
};

struct PACKED tu_autotune::config_t {
 private:
   algorithm algo = algorithm::DEFAULT;
   uint8_t mod_flags = 0;    /* See mod_flag enum. */
   uint8_t metric_flags = 0; /* See metric_flag enum. */

   constexpr void update_metric_flags()
   {
      /* Note: Always keep in sync with rp_history to prevent UB. */
      if (algo == algorithm::BANDWIDTH) {
         metric_flags |= (uint8_t) metric_flag::SAMPLES;
      }
   }

 public:
   constexpr config_t() = default;

   constexpr config_t(algorithm algo, uint8_t mod_flags): algo(algo), mod_flags(mod_flags)
   {
      update_metric_flags();
   }

   constexpr bool is_enabled(algorithm a) const
   {
      return algo == a;
   }

   constexpr bool test(mod_flag f) const
   {
      return mod_flags & (uint32_t) f;
   }

   constexpr bool test(metric_flag f) const
   {
      return metric_flags & (uint32_t) f;
   }

   constexpr bool set_algo(algorithm a)
   {
      if (algo == a)
         return false;

      algo = a;
      update_metric_flags();
      return true;
   }

   constexpr bool disable(mod_flag f)
   {
      if (!(mod_flags & (uint8_t) f))
         return false;

      mod_flags &= ~(uint8_t) f;
      update_metric_flags();
      return true;
   }

   constexpr bool enable(mod_flag f)
   {
      if (mod_flags & (uint8_t) f)
         return false;

      mod_flags |= (uint8_t) f;
      update_metric_flags();
      return true;
   }

   std::string to_string() const
   {
#define ALGO_STR(algo_name)                                                                                            \
   if (algo == algorithm::algo_name)                                                                                   \
      str += #algo_name;
#define MODF_STR(flag)                                                                                                 \
   if (mod_flags & (uint8_t) mod_flag::flag) {                                                                         \
      str += #flag " ";                                                                                                \
   }
#define METRICF_STR(flag)                                                                                              \
   if (metric_flags & (uint8_t) metric_flag::flag) {                                                                   \
      str += #flag " ";                                                                                                \
   }

      std::string str = "Algorithm: ";

      ALGO_STR(BANDWIDTH);

      str += ", Mod Flags: 0x" + std::to_string(mod_flags) + " (";
      MODF_STR(BIG_GMEM);
      MODF_STR(SMALL_SYSMEM);
      str += ")";

      str += ", Metric Flags: 0x" + std::to_string(metric_flags) + " (";
      METRICF_STR(SAMPLES);
      str += ")";

      return str;

#undef ALGO_STR
#undef MODF_STR
#undef METRICF_STR
   }
};

union PACKED tu_autotune::packed_config_t {
   config_t config;
   uint32_t bits = 0;
   static_assert(sizeof(bits) >= sizeof(config));
   static_assert(std::is_trivially_copyable<config_t>::value,
                 "config_t must be trivially copyable to be automatically packed");

   constexpr packed_config_t(config_t p_config): bits(0)
   {
      config = p_config; /* Set after bits(0) to avoid UB in sizeof(bits) > sizeof(config) case.*/
   }

   constexpr packed_config_t(uint32_t bits): bits(bits)
   {
   }
};

tu_autotune::atomic_config_t::atomic_config_t(config_t initial): config_bits(packed_config_t { initial }.bits)
{
}

tu_autotune::config_t
tu_autotune::atomic_config_t::load() const
{
   return config_t(packed_config_t { config_bits.load(std::memory_order_relaxed) }.config);
}

bool
tu_autotune::atomic_config_t::compare_and_store(config_t expected, config_t updated)
{
   uint32_t expected_bits = packed_config_t { expected }.bits;
   return config_bits.compare_exchange_strong(expected_bits, packed_config_t { updated }.bits,
                                              std::memory_order_acquire, std::memory_order_relaxed);
}

tu_autotune::config_t
tu_autotune::get_env_config()
{
   static std::once_flag once;
   static config_t at_config;
   std::call_once(once, [&] {
      const char *algo_env_str = os_get_option("TU_AUTOTUNE_ALGO");
      algorithm algo = algorithm::DEFAULT;

      if (algo_env_str) {
         std::string_view algo_strv(algo_env_str);
         if (algo_strv == "bandwidth") {
            algo = algorithm::BANDWIDTH;
         }

         if (TU_DEBUG(STARTUP))
            mesa_logi("TU_AUTOTUNE_ALGO=%u (%s)", (uint8_t) algo, algo_env_str);
      }

      /* Parse the flags from the environment variable. */
      const char *flags_env_str = os_get_option("TU_AUTOTUNE_FLAGS");
      uint32_t mod_flags = 0;
      if (flags_env_str) {
         static const struct debug_control tu_at_flags_control[] = {
            { "big_gmem", (uint32_t) mod_flag::BIG_GMEM },
            { "small_sysmem", (uint32_t) mod_flag::SMALL_SYSMEM },
            { NULL, 0 }
         };

         mod_flags = parse_debug_string(flags_env_str, tu_at_flags_control);
         if (TU_DEBUG(STARTUP))
            mesa_logi("TU_AUTOTUNE_FLAGS=0x%x (%s)", mod_flags, flags_env_str);
      }

      assert((uint8_t) mod_flags == mod_flags);
      at_config = config_t(algo, (uint8_t) mod_flags);
   });

   if (TU_DEBUG(STARTUP))
      mesa_logi("TU_AUTOTUNE: %s", at_config.to_string().c_str());

   return at_config;
}

/** Global Fence and Internal CS Management **/

tu_autotune::submission_entry::submission_entry(tu_device *device): fence(0)
{
   tu_cs_init(&fence_cs, device, TU_CS_MODE_GROW, 5, "autotune fence cs");
}

tu_autotune::submission_entry::~submission_entry()
{
   assert(!is_active());
   tu_cs_finish(&fence_cs);
}

bool
tu_autotune::submission_entry::is_active() const
{
   return fence_cs.device->global_bo_map->autotune_fence < fence;
}

template <chip CHIP>
static void
write_fence_cs(struct tu_device *dev, struct tu_cs *cs, uint32_t fence)
{
   uint64_t dst_iova = dev->global_bo->iova + gb_offset(autotune_fence);
   if (CHIP >= A7XX) {
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 4);
      tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = CACHE_FLUSH_TS, .write_src = EV_WRITE_USER_32B, .write_dst = EV_DST_RAM,
                                       .write_enabled = true)
                        .value);
   } else {
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 4);
      tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(CACHE_FLUSH_TS));
   }

   tu_cs_emit_qw(cs, dst_iova);
   tu_cs_emit(cs, fence);
}

struct tu_cs *
tu_autotune::submission_entry::try_get_cs(uint32_t new_fence)
{
   if (is_active()) {
      /* If the CS is already active, we cannot write to it. */
      return nullptr;
   }

   struct tu_device *device = fence_cs.device;
   tu_cs_reset(&fence_cs);
   tu_cs_begin(&fence_cs);
   TU_CALLX(device, write_fence_cs)(device, &fence_cs, new_fence);
   tu_cs_end(&fence_cs);
   assert(fence_cs.entry_count == 1); /* We expect the initial allocation to be large enough. */
   fence = new_fence;

   return &fence_cs;
}

struct tu_cs *
tu_autotune::get_cs_for_fence(uint32_t fence)
{
   for (submission_entry &entry : submission_entries) {
      struct tu_cs *cs = entry.try_get_cs(fence);
      if (cs)
         return cs;
   }

   /* If we reach here, we have to allocate a new entry. */
   submission_entry &entry = submission_entries.emplace_back(device);
   struct tu_cs *cs = entry.try_get_cs(fence);
   assert(cs); /* We just allocated it, so it should be available. */
   return cs;
}

/** RP Entry Management **/

/* The part of the per-RP entry which is written by the GPU. */
struct PACKED tu_autotune::rp_gpu_data {
   /* HW requires the sample start/stop locations to be 128b aligned. */
   alignas(16) uint64_t samples_start;
   alignas(16) uint64_t samples_end;
   uint64_t ts_start;
   uint64_t ts_end;
};

/* A small wrapper around rp_history to provide ref-counting and usage timestamps. */
struct tu_autotune::rp_history_handle {
   rp_history *history;

   /* Note: Must be called with rp_mutex held. */
   rp_history_handle(rp_history &history);

   constexpr rp_history_handle(std::nullptr_t): history(nullptr)
   {
   }

   rp_history_handle(const rp_history_handle &) = delete;
   rp_history_handle &operator=(const rp_history_handle &) = delete;

   constexpr rp_history_handle(rp_history_handle &&other): history(other.history)
   {
      other.history = nullptr;
   }

   constexpr rp_history_handle &operator=(rp_history_handle &&other)
   {
      if (this != &other) {
         history = other.history;
         other.history = nullptr;
      }
      return *this;
   }

   constexpr operator bool() const
   {
      return history != nullptr;
   }

   constexpr rp_history &operator*() const
   {
      assert(history);
      return *history;
   }

   constexpr operator rp_history *() const
   {
      return history;
   }

   constexpr rp_history *operator->() const
   {
      assert(history);
      return history;
   }

   ~rp_history_handle();
};

/* An "entry" of renderpass autotune results, which is used to store the results of a renderpass autotune run for a
 * given command buffer. */
struct tu_autotune::rp_entry {
 private:
   struct tu_device *device;

   struct tu_suballoc_bo bo;
   uint8_t *map; /* A direct pointer to the BO's CPU mapping. */

   static_assert(alignof(rp_gpu_data) == 16);
   static_assert(offsetof(rp_gpu_data, samples_start) == 0);
   static_assert(offsetof(rp_gpu_data, samples_end) == 16);

 public:
   rp_history_handle history;
   config_t config; /* Configuration at the time of entry creation. */
   bool sysmem;
   uint32_t draw_count;

   rp_entry(struct tu_device *device, rp_history_handle &&history, config_t config, uint32_t draw_count)
       : device(device), map(nullptr), history(std::move(history)), config(config), draw_count(draw_count)
   {
   }

   ~rp_entry()
   {
      if (map) {
         std::scoped_lock lock(device->autotune->suballoc_mutex);
         tu_suballoc_bo_free(&device->autotune->suballoc, &bo);
      }
   }

   /* Disable the copy/move operators as that shouldn't be done. */
   rp_entry(const rp_entry &) = delete;
   rp_entry &operator=(const rp_entry &) = delete;
   rp_entry(rp_entry &&) = delete;
   rp_entry &operator=(rp_entry &&) = delete;

   void allocate(bool sysmem)
   {
      this->sysmem = sysmem;
      size_t total_size = sizeof(rp_gpu_data);

      std::scoped_lock lock(device->autotune->suballoc_mutex);
      VkResult result = tu_suballoc_bo_alloc(&bo, &device->autotune->suballoc, total_size, alignof(rp_gpu_data));
      if (result != VK_SUCCESS) {
         mesa_loge("Failed to allocate BO for autotune rp_entry: %u", result);
         return;
      }

      map = (uint8_t *) tu_suballoc_bo_map(&bo);
      memset(map, 0, total_size);
   }

   rp_gpu_data &get_gpu_data()
   {
      assert(map);
      return *(rp_gpu_data *) map;
   }

   /** Samples-Passed Metric **/

   uint64_t get_samples_passed()
   {
      assert(config.test(metric_flag::SAMPLES));
      rp_gpu_data &gpu = get_gpu_data();
      return gpu.samples_end - gpu.samples_start;
   }

   void emit_metric_samples_start(struct tu_cmd_buffer *cmd, struct tu_cs *cs, uint64_t start_iova)
   {
      tu_cs_emit_regs(cs, A6XX_RB_SAMPLE_COUNTER_CNTL(.copy = true));
      if (cmd->device->physical_device->info->props.has_event_write_sample_count) {
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 3);
         tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = ZPASS_DONE, .write_sample_count = true).value);
         tu_cs_emit_qw(cs, start_iova);

         /* If the renderpass contains an occlusion query with its own ZPASS_DONE, we have to provide a fake ZPASS_DONE
          * event here to logically close the previous one, preventing firmware from misbehaving due to nested events.
          * This writes into the samples_end field, which will be overwritten in tu_autotune_end_renderpass.
          */
         if (cmd->state.rp.has_zpass_done_sample_count_write_in_rp) {
            tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 3);
            tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = ZPASS_DONE, .write_sample_count = true,
                                             .sample_count_end_offset = true, .write_accum_sample_count_diff = true)
                              .value);
            tu_cs_emit_qw(cs, start_iova);
         }
      } else {
         tu_cs_emit_regs(cs, A6XX_RB_SAMPLE_COUNTER_BASE(.qword = start_iova));
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
         tu_cs_emit(cs, ZPASS_DONE);
      }
   }

   void emit_metric_samples_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs, uint64_t start_iova, uint64_t end_iova)
   {
      tu_cs_emit_regs(cs, A6XX_RB_SAMPLE_COUNTER_CNTL(.copy = true));
      if (cmd->device->physical_device->info->props.has_event_write_sample_count) {
         /* If the renderpass contains ZPASS_DONE events we emit a fake ZPASS_DONE event here, composing a pair of these
          * events that firmware handles without issue. This first event writes into the samples_end field and the
          * second event overwrites it. The second event also enables the accumulation flag even when we don't use that
          * result because the blob always sets it.
          */
         if (cmd->state.rp.has_zpass_done_sample_count_write_in_rp) {
            tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 3);
            tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = ZPASS_DONE, .write_sample_count = true).value);
            tu_cs_emit_qw(cs, end_iova);
         }

         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 3);
         tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = ZPASS_DONE, .write_sample_count = true,
                                          .sample_count_end_offset = true, .write_accum_sample_count_diff = true)
                           .value);
         tu_cs_emit_qw(cs, start_iova);
      } else {
         tu_cs_emit_regs(cs, A6XX_RB_SAMPLE_COUNTER_BASE(.qword = end_iova));
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
         tu_cs_emit(cs, ZPASS_DONE);
      }
   }

   /** CS Emission **/

   void emit_rp_start(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
   {
      assert(map && bo.iova);
      uint64_t bo_iova = bo.iova;
      if (config.test(metric_flag::SAMPLES))
         emit_metric_samples_start(cmd, cs, bo_iova + offsetof(rp_gpu_data, samples_start));
   }

   void emit_rp_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
   {
      assert(map && bo.iova);
      uint64_t bo_iova = bo.iova;
      if (config.test(metric_flag::SAMPLES))
         emit_metric_samples_end(cmd, cs, bo_iova + offsetof(rp_gpu_data, samples_start),
                                 bo_iova + offsetof(rp_gpu_data, samples_end));
   }
};

tu_autotune::rp_entry_batch::rp_entry_batch(): active(false), fence(0), entries()
{
}

void
tu_autotune::rp_entry_batch::assign_fence(uint32_t new_fence)
{
   assert(!active); /* Cannot assign a fence to an active entry batch. */
   fence = new_fence;
   active = true;
}

void
tu_autotune::rp_entry_batch::mark_inactive()
{
   assert(active);
   active = false;
   fence = 0;
}

/** Renderpass state tracking. **/

tu_autotune::rp_key::rp_key(const struct tu_render_pass *pass,
                            const struct tu_framebuffer *framebuffer,
                            const struct tu_cmd_buffer *cmd)
{
   /* Q: Why not make the key from framebuffer + renderpass pointers?
    * A: At least DXVK creates new framebuffers each frame while keeping renderpasses the same. Hashing the contents
    *    of the framebuffer and renderpass is more stable, and it maintains stability across runs, so we can reliably
    *    identify the same renderpass instance.
    */

   auto get_hash = [&](uint32_t *data, size_t size) {
      uint32_t *ptr = data;
      *ptr++ = framebuffer->width;
      *ptr++ = framebuffer->height;
      *ptr++ = framebuffer->layers;

      for (unsigned i = 0; i < pass->attachment_count; i++) {
         *ptr++ = cmd->state.attachments[i]->view.width;
         *ptr++ = cmd->state.attachments[i]->view.height;
         *ptr++ = cmd->state.attachments[i]->image->vk.format;
         *ptr++ = cmd->state.attachments[i]->image->vk.array_layers;
         *ptr++ = cmd->state.attachments[i]->image->vk.mip_levels;
      }

      return XXH3_64bits(data, size * sizeof(uint32_t));
   };

   /* We do a manual Boost-style "small vector" optimization here where the stack is used for the vast majority of
    * cases, while only extreme cases need to allocate on the heap.
    */
   size_t data_count = 3 + (pass->attachment_count * 5);
   constexpr size_t STACK_MAX_DATA_COUNT = 3 + (5 * 5); /* in u32 units. */

   if (data_count <= STACK_MAX_DATA_COUNT) {
      /* If the data is small enough, we can use the stack. */
      std::array<uint32_t, STACK_MAX_DATA_COUNT> arr;
      hash = get_hash(arr.data(), data_count);
   } else {
      /* If the data is too large, we have to allocate it on the heap. */
      std::vector<uint32_t> vec(data_count);
      hash = get_hash(vec.data(), vec.size());
   }
}

/* Exponential moving average (EMA) calculator for smoothing successive values of any metric. An alpha (smoothing
 * factor) of 0.1 means 10% weight to new values (slow adaptation), while 0.9 means 90% weight (fast adaptation).
 */
template <typename T = double> class exponential_average {
 private:
   std::atomic<double> average = std::numeric_limits<double>::quiet_NaN();
   double alpha;

 public:
   explicit exponential_average(double alpha = 0.1) noexcept: alpha(alpha)
   {
   }

   bool empty() const noexcept
   {
      double current = average.load(std::memory_order_relaxed);
      return std::isnan(current);
   }

   void add(T value) noexcept
   {
      double v = static_cast<double>(value);
      double current = average.load(std::memory_order_relaxed);
      double new_avg;
      do {
         new_avg = std::isnan(current) ? v : (1.0 - alpha) * current + alpha * v;
      } while (!average.compare_exchange_weak(current, new_avg, std::memory_order_relaxed, std::memory_order_relaxed));
   }

   void clear() noexcept
   {
      average.store(std::numeric_limits<double>::quiet_NaN(), std::memory_order_relaxed);
   }

   T get() const noexcept
   {
      double current = average.load(std::memory_order_relaxed);
      return std::isnan(current) ? T {} : static_cast<T>(current);
   }
};

/* All historical state pertaining to a uniquely identified RP. This integrates data from RP entries, accumulating
 * metrics over the long-term and providing autotune algorithms using the data.
 */
struct tu_autotune::rp_history {
 public:
   uint64_t hash; /* The hash of the renderpass, just for debug output. */

   std::atomic<uint32_t> refcount = 0; /* Reference count to prevent deletion when active. */
   std::atomic<uint64_t> last_use_ts;  /* Last time the reference count was updated, in monotonic nanoseconds. */

   rp_history(uint64_t hash): hash(hash), last_use_ts(os_time_get_nano())
   {
   }

   /** Bandwidth Estimation Algorithm **/
   struct bandwidth_algo {
    private:
      exponential_average<uint32_t> mean_samples_passed;

    public:
      void update(uint32_t samples)
      {
         mean_samples_passed.add(samples);
      }

      render_mode get_optimal_mode(rp_history &history,
                                   const struct tu_cmd_state *cmd_state,
                                   const struct tu_render_pass *pass,
                                   const struct tu_framebuffer *framebuffer,
                                   const struct tu_render_pass_state *rp_state)
      {
         uint32_t pass_pixel_count = 0;
         if (cmd_state->per_layer_render_area) {
            for (unsigned i = 0; i < cmd_state->pass->num_views; i++) {
               const VkExtent2D &extent = cmd_state->render_areas[i].extent;
               pass_pixel_count += extent.width * extent.height;
            }
         } else {
            const VkExtent2D &extent = cmd_state->render_areas[0].extent;
            pass_pixel_count =
               extent.width * extent.height * MAX2(cmd_state->pass->num_views, cmd_state->framebuffer->layers);
         }

         uint64_t sysmem_bandwidth = (uint64_t) pass->sysmem_bandwidth_per_pixel * pass_pixel_count;
         uint64_t gmem_bandwidth = (uint64_t) pass->gmem_bandwidth_per_pixel * pass_pixel_count;

         uint64_t total_draw_call_bandwidth = 0;
         uint64_t mean_samples = mean_samples_passed.get();
         if (rp_state->drawcall_count && mean_samples > 0.0) {
            /* The total draw call bandwidth is estimated as the average samples (collected via tracking samples passed
             * within the CS) multiplied by the drawcall bandwidth per sample, divided by the amount of draw calls.
             *
             * This is a rough estimate of the bandwidth used by the draw calls in the renderpass for FB writes which
             * is used to determine whether to use SYSMEM or GMEM.
             */
            total_draw_call_bandwidth =
               (mean_samples * rp_state->drawcall_bandwidth_per_sample_sum) / rp_state->drawcall_count;
         }

         /* Drawcalls access the memory in SYSMEM rendering (ignoring CCU). */
         sysmem_bandwidth += total_draw_call_bandwidth;

         /* Drawcalls access GMEM in GMEM rendering, but we do not want to ignore them completely.  The state changes
          * between tiles also have an overhead.  The magic numbers of 11 and 10 are randomly chosen.
          */
         gmem_bandwidth = (gmem_bandwidth * 11 + total_draw_call_bandwidth) / 10;

         bool select_sysmem = sysmem_bandwidth <= gmem_bandwidth;
         render_mode mode = select_sysmem ? render_mode::SYSMEM : render_mode::GMEM;

         UNUSED const VkExtent2D &extent = cmd_state->render_areas[0].extent;
         at_log_bandwidth_h(
            "%" PRIu32 " selecting %s\n"
            "   mean_samples=%" PRIu64 ", draw_bandwidth_per_sample=%.2f, total_draw_call_bandwidth=%" PRIu64
            ", render_areas[0]=%" PRIu32 "x%" PRIu32 ", sysmem_bandwidth_per_pixel=%" PRIu32
            ", gmem_bandwidth_per_pixel=%" PRIu32 ", sysmem_bandwidth=%" PRIu64 ", gmem_bandwidth=%" PRIu64,
            history.hash, rp_state->drawcall_count, render_mode_str(mode), mean_samples,
            (float) rp_state->drawcall_bandwidth_per_sample_sum / rp_state->drawcall_count, total_draw_call_bandwidth,
            extent.width, extent.height, pass->sysmem_bandwidth_per_pixel, pass->gmem_bandwidth_per_pixel,
            sysmem_bandwidth, gmem_bandwidth);

         return mode;
      }
   } bandwidth;

   void process(rp_entry &entry, tu_autotune &at)
   {
      /* We use entry config to know what metrics it has, autotune config to know what algorithms are enabled. */
      config_t entry_config = entry.config;
      config_t at_config = at.active_config.load();

      if (entry_config.test(metric_flag::SAMPLES) && at_config.is_enabled(algorithm::BANDWIDTH))
         bandwidth.update(entry.get_samples_passed());
   }
};

tu_autotune::rp_history_handle::~rp_history_handle()
{
   if (!history)
      return;

   history->last_use_ts.store(os_time_get_nano(), std::memory_order_relaxed);
   ASSERTED uint32_t old_refcount = history->refcount.fetch_sub(1, std::memory_order_relaxed);
   assert(old_refcount != 0); /* Underflow check. */
}

tu_autotune::rp_history_handle::rp_history_handle(rp_history &history): history(&history)
{
   history.refcount.fetch_add(1, std::memory_order_relaxed);
   history.last_use_ts.store(os_time_get_nano(), std::memory_order_relaxed);
}

tu_autotune::rp_history_handle
tu_autotune::find_rp_history(const rp_key &key)
{
   std::shared_lock lock(rp_mutex);
   auto it = rp_histories.find(key);
   if (it != rp_histories.end())
      return rp_history_handle(it->second);

   return rp_history_handle(nullptr);
}

tu_autotune::rp_history_handle
tu_autotune::find_or_create_rp_history(const rp_key &key)
{
   rp_history *existing = find_rp_history(key);
   if (existing)
      return *existing;

   /* If we reach here, we have to create a new history. */
   std::unique_lock lock(rp_mutex);
   auto it = rp_histories.find(key);
   if (it != rp_histories.end())
      return it->second; /* Another thread created the history while we were waiting for the lock. */
   auto history = rp_histories.emplace(std::make_pair(key, key.hash));
   return rp_history_handle(history.first->second);
}

void
tu_autotune::reap_old_rp_histories()
{
   constexpr uint64_t REAP_INTERVAL_NS = 10'000'000'000; /* 10s */
   uint64_t now = os_time_get_nano();
   if (last_reap_ts + REAP_INTERVAL_NS > now)
      return;
   last_reap_ts = now;

   constexpr size_t MAX_RP_HISTORIES = 1024; /* Not a hard limit, we might exceed this if there's many active RPs. */
   {
      /* Quicker non-unique lock, should hit this path mostly. */
      std::shared_lock lock(rp_mutex);
      if (rp_histories.size() <= MAX_RP_HISTORIES)
         return;
   }

   std::unique_lock lock(rp_mutex);
   size_t og_size = rp_histories.size();
   if (og_size <= MAX_RP_HISTORIES)
      return;

   std::vector<rp_histories_t::iterator> candidates;
   candidates.reserve(og_size);
   for (auto it = rp_histories.begin(); it != rp_histories.end(); ++it) {
      if (it->second.refcount.load(std::memory_order_relaxed) == 0)
         candidates.push_back(it);
   }

   size_t to_purge = std::min(candidates.size(), og_size - MAX_RP_HISTORIES);
   if (to_purge == 0) {
      at_log_base("no RP histories to reap at size %zu, all are active", og_size);
      return;
   }

   /* Partition candidates by last use timestamp, oldest first. */
   auto partition_end = candidates.begin() + to_purge;
   if (to_purge < candidates.size()) {
      std::nth_element(candidates.begin(), partition_end, candidates.end(),
                       [](rp_histories_t::iterator a, rp_histories_t::iterator b) {
                          return a->second.last_use_ts.load(std::memory_order_relaxed) <
                                 b->second.last_use_ts.load(std::memory_order_relaxed);
                       });
   }

   for (auto it = candidates.begin(); it != partition_end; ++it) {
      rp_history &history = (*it)->second;
      if (history.refcount.load(std::memory_order_relaxed) == 0) {
         at_log_base("reaping RP history %016" PRIx64, history.hash);
         rp_histories.erase(*it);
      }
   }

   at_log_base("reaped old RP histories %zu -> %zu", og_size, rp_histories.size());
}

void
tu_autotune::process_entries()
{
   uint32_t current_fence = device->global_bo_map->autotune_fence;

   while (!active_batches.empty()) {
      auto &batch = active_batches.front();
      assert(batch->active);

      if (fence_before(current_fence, batch->fence))
         break; /* Entries are allocated in sequence, next will be newer and
                   also fail so we can just directly break out of the loop. */

      for (auto &entry : batch->entries)
         entry->history->process(*entry, *this);

      batch->mark_inactive();
      active_batches.pop_front();
   }

   if (active_batches.size() > 10) {
      at_log_base("high amount of active batches: %zu, fence: %" PRIu32 " < %" PRIu32, active_batches.size(),
                  current_fence, active_batches.front()->fence);
   }
}

struct tu_cs *
tu_autotune::on_submit(struct tu_cmd_buffer **cmd_buffers, uint32_t cmd_buffer_count)
{

   /* This call occurs regularly and we are single-threaded here, so we use this opportunity to process any available
    * entries. It's also important that any entries are processed here because we always want to ensure that we've
    * processed all entries from prior CBs before we submit any new CBs with the same RP to the GPU.
    */
   process_entries();
   reap_old_rp_histories();

   bool has_results = false;
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      auto &batch = cmd_buffers[i]->autotune_ctx.batch;
      if (!batch->entries.empty()) {
         has_results = true;
         break;
      }
   }
   if (!has_results)
      return nullptr; /* No results to process, return early. */

   /* Generate a new fence and the CS for it. */
   const uint32_t new_fence = next_fence++;
   auto fence_cs = get_cs_for_fence(new_fence);
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      /* Transfer the entries from the command buffers to the active queue. */
      struct tu_cmd_buffer *cmdbuf = cmd_buffers[i];
      auto &batch = cmdbuf->autotune_ctx.batch;
      if (batch->entries.empty())
         continue;

      batch->assign_fence(new_fence);
      if (cmdbuf->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) {
         /* If the command buffer is one-time submit, we can move the batch directly into the active batches, as it
          * won't be used again. This would lead to it being deallocated as early as possible.
          */
         active_batches.push_back(std::move(batch));
      } else {
         active_batches.push_back(batch);
      }
   }

   return fence_cs;
}

tu_autotune::tu_autotune(struct tu_device *device, VkResult &result): device(device), active_config(get_env_config())
{
   tu_bo_suballocator_init(&suballoc, device, 128 * 1024, TU_BO_ALLOC_INTERNAL_RESOURCE, "autotune_suballoc");

   result = VK_SUCCESS;
   return;
}

tu_autotune::~tu_autotune()
{
   if (TU_AUTOTUNE_FLUSH_AT_FINISH) {
      while (!active_batches.empty())
         process_entries();
      at_log_base("finished processing all entries");
   }

   tu_bo_suballocator_finish(&suballoc);
}

tu_autotune::cmd_buf_ctx::cmd_buf_ctx(): batch(std::make_shared<rp_entry_batch>())
{
}

tu_autotune::cmd_buf_ctx::~cmd_buf_ctx()
{
   /* This is empty but it causes the implicit destructor to be compiled within this compilation unit with access to
    * internal structures. Otherwise, we would need to expose the full definition of autotuner internals in the header
    * file, which is not desirable.
    */
}

void
tu_autotune::cmd_buf_ctx::reset()
{
   batch = std::make_shared<rp_entry_batch>();
}

tu_autotune::rp_entry *
tu_autotune::cmd_buf_ctx::attach_rp_entry(struct tu_device *device,
                                          rp_history_handle &&history,
                                          config_t config,
                                          uint32_t drawcall_count)
{
   std::unique_ptr<rp_entry> &new_entry =
      batch->entries.emplace_back(std::make_unique<rp_entry>(device, std::move(history), config, drawcall_count));
   return new_entry.get();
}

tu_autotune::render_mode
tu_autotune::get_optimal_mode(struct tu_cmd_buffer *cmd_buffer, rp_ctx_t *rp_ctx)
{
   const struct tu_cmd_state *cmd_state = &cmd_buffer->state;
   const struct tu_render_pass *pass = cmd_state->pass;
   const struct tu_framebuffer *framebuffer = cmd_state->framebuffer;
   const struct tu_render_pass_state *rp_state = &cmd_state->rp;
   cmd_buf_ctx &cb_ctx = cmd_buffer->autotune_ctx;
   config_t config = active_config.load();

   /* Just to ensure a segfault for accesses, in case we don't set it. */
   *rp_ctx = nullptr;

   /* If a feedback loop in the subpass caused one of the pipelines used to set
    * SINGLE_PRIM_MODE(FLUSH_PER_OVERLAP_AND_OVERWRITE) or even SINGLE_PRIM_MODE(FLUSH), then that should cause
    * significantly increased SYSMEM bandwidth (though we haven't quantified it).
    */
   if (rp_state->sysmem_single_prim_mode)
      return render_mode::GMEM;

   /* If the user is using a fragment density map, then this will cause less FS invocations with GMEM, which has a
    * hard-to-measure impact on performance because it depends on how heavy the FS is in addition to how many
    * invocations there were and the density. Let's assume the user knows what they're doing when they added the map,
    * because if SYSMEM is actually faster then they could've just not used the fragment density map.
    */
   if (pass->has_fdm)
      return render_mode::GMEM;

   /* SYSMEM is always a safe default mode when we can't fully engage the autotuner. From testing, we know that for an
    * incorrect decision towards SYSMEM tends to be far less impactful than an incorrect decision towards GMEM, which
    * can cause significant performance issues.
    */
   constexpr render_mode default_mode = render_mode::SYSMEM;

   /* For VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT buffers, we would have to allocate GPU memory at the submit time
    * and copy results into it. We just disable complex autotuner in this case, which isn't a big issue since native
    * games usually don't use it, Zink and DXVK don't use it, while D3D12 doesn't even have such concept.
    *
    * We combine this with processing entries at submit time, to avoid a race where the CPU hasn't processed the results
    * from an earlier submission of the CB while a second submission of the CB is on the GPU queue.
    */
   bool simultaneous_use = cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

   if (!enabled || simultaneous_use)
      return default_mode;

   if (config.test(mod_flag::BIG_GMEM) && rp_state->drawcall_count >= 10)
      return render_mode::GMEM;
   if (config.test(mod_flag::SMALL_SYSMEM) && rp_state->drawcall_count <= 5)
      return render_mode::SYSMEM;

   rp_key key(pass, framebuffer, cmd_buffer);
   *rp_ctx = cb_ctx.attach_rp_entry(device, find_or_create_rp_history(key), config, rp_state->drawcall_count);
   rp_history &history = *((*rp_ctx)->history);

   if (config.is_enabled(algorithm::BANDWIDTH))
      return history.bandwidth.get_optimal_mode(history, cmd_state, pass, framebuffer, rp_state);

   return default_mode;
}

/** RP-level CS emissions **/

void
tu_autotune::begin_renderpass(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx, bool sysmem)
{
   if (!rp_ctx)
      return;

   rp_ctx->allocate(sysmem);
   rp_ctx->emit_rp_start(cmd, cs);
}

void
tu_autotune::end_renderpass(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx)
{
   if (!rp_ctx)
      return;

   rp_ctx->emit_rp_end(cmd, cs);
}
