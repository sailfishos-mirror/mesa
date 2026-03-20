/*
 * Copyright © 2021 Collabora, Ltd.
 * Copyright © 2026 Arm, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_pps_perf.h"
#include "pan_pps_driver.h"

#include <lib/kmod/pan_kmod.h>
#include <perf/pan_perf.h>

#include <pps/pps.h>
#include <util/ralloc.h>
#include <util/timespec.h>

namespace pps {

PanfrostPerf::PanfrostPerf(const PanfrostDevice &dev)
{
   assert(dev.fd >= 0);
   perf = pan_perf_create(dev.fd);
   assert(perf);
}

PanfrostPerf::~PanfrostPerf()
{
   if (perf) {
      pan_perf_disable(perf);
      pan_perf_destroy(perf);
   }
}

PanfrostPerf::PanfrostPerf(PanfrostPerf &&o): perf{o.perf}
{
   o.perf = nullptr;
}

PanfrostPerf &
PanfrostPerf::operator=(PanfrostPerf &&o)
{
   std::swap(perf, o.perf);
   return *this;
}

int
PanfrostPerf::enable(uint64_t /* sampling_period_ns */)
{
   assert(perf);
   return pan_perf_enable(perf);
}

void
PanfrostPerf::disable()
{
   assert(perf);
   pan_perf_disable(perf);
}

bool
PanfrostPerf::dump()
{
   assert(perf);
   last_dump_ts = perfetto::base::GetBootTimeNs().count();

   int ret = pan_perf_dump(perf);

   return !!(ret >= 0);
}

uint64_t
PanfrostPerf::get_min_sampling_period_ns()
{
   return 1000000;
}

void *
PanfrostPerf::get_subinstance() {
   return perf;
}

std::string
format_suffix(const char *fmt, uint8_t idx)
{
   assert(strlen(fmt) < 200 && "fmt unreasonably long");
   char buf[256];
   std::snprintf(buf, sizeof(buf), fmt, idx);

   return std::string(buf);
}

const char *
get_block_suffix(uint8_t category)
{
   assert(category <= MALI_PERF_BLOCK_TYPE_COUNT);

   switch (category) {
   case MALI_PERF_BLOCK_MEMSYS:
      return " (slice %u)";
   case MALI_PERF_BLOCK_SHADER_CORE:
      return " (core %u)";
   default:
      return nullptr;
   }

   return nullptr;
}

Counter::Units
convert_pan_units(enum mali_perf_counter_units unit)
{
   switch (unit) {
   case MALI_PERF_COUNTER_UNITS_PRIMITIVES:
      return Counter::Units::Primitive;
   case MALI_PERF_COUNTER_UNITS_INSTRUCTIONS:
      return Counter::Units::Instruction;
   case MALI_PERF_COUNTER_UNITS_BYTES:
      return Counter::Units::Byte;
   case MALI_PERF_COUNTER_UNITS_PIXELS:
      return Counter::Units::Pixel;
   default:
      return Counter::Units::None;
   }
}

std::pair<std::vector<CounterGroup>, std::vector<Counter>>
PanfrostPerf::create_available_counters() const
{
   std::pair<std::vector<CounterGroup>, std::vector<Counter>> ret;
   auto &[groups, counters] = ret;

   uint32_t global_counter_id = 0;

   const struct pan_perf_category *category = NULL;
   for (uint32_t cat_idx = 0; cat_idx < perf->cfg->n_categories; ++cat_idx) {
      assert(cat_idx < MALI_PERF_BLOCK_TYPE_COUNT);
      category = &perf->cfg->categories[cat_idx];

      if (!category->n_counters)
         continue;

      enum mali_perf_block_type blk_type = category->counters[0].category_id;
      CounterGroup group = {};
      group.id = blk_type;
      group.name = mali_perf_block_type_str(blk_type);

      uint32_t n_blocks = mali_perf_block_count(blk_type, &perf->constants);
      for (uint32_t counter_idx = 0; counter_idx < category->n_counters;
           ++counter_idx) {
         const struct pan_perf_counter *cinfo =
            &category->counters[counter_idx];

         for (uint32_t block_idx = 0; block_idx < n_blocks; ++block_idx) {
            const char *suffix = get_block_suffix(group.id);
            const std::string name =
               cinfo->name + (suffix ? format_suffix(suffix, block_idx) : "");

            Counter counter = {};
            counter.id = global_counter_id++;
            counter.name = name;
            counter.group = group.id;
            counter.units = convert_pan_units(cinfo->units);

            counter.set_getter([=](const Counter &c, const Driver &d) {
               auto &pan_driver = PanfrostDriver::into(d);
               struct pan_perf *perf = static_cast<struct pan_perf *>(
                  pan_driver.perf->get_subinstance());
               return pan_perf_counter_read(perf, cinfo, block_idx);
            });

            group.counters.push_back(counter.id);
            counters.emplace_back(counter);
         }
      }

      groups.push_back(group);
   }

   return ret;
}

uint64_t
PanfrostPerf::next()
{
   auto ret = last_dump_ts;
   last_dump_ts = 0;
   return ret;
}

uint32_t
PanfrostPerf::gpu_clock_id() const
{
   return perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
}

uint64_t
PanfrostPerf::gpu_timestamp() const
{
   return perfetto::base::GetBootTimeNs().count();
}

bool
PanfrostPerf::cpu_gpu_timestamp(uint64_t &, uint64_t &) const
{
   /* Not supported */
   return false;
}

} // namespace pps
