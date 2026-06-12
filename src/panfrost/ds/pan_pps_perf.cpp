/*
 * Copyright © 2021 Collabora, Ltd.
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

std::pair<std::vector<CounterGroup>, std::vector<Counter>>
PanfrostPerf::create_available_counters() const
{
   std::pair<std::vector<CounterGroup>, std::vector<Counter>> ret;
   auto &[groups, counters] = ret;

   size_t cid = 0;

   for (uint32_t gid = 0; gid < perf->cfg->n_categories; ++gid) {
      const auto &category = perf->cfg->categories[gid];
      CounterGroup group = {};
      group.id = gid;
      group.name = category.name;

      for (size_t id = 0; cid < category.n_counters; ++cid) {
         Counter counter = {};
         counter.id = cid;
         counter.group = gid;

         counter.name = category.counters[id].name;

         counter.set_getter([=](const Counter &c, const Driver &d) {
            auto &pan_driver = PanfrostDriver::into(d);
            struct pan_perf *perf = static_cast<struct pan_perf *>(
                  pan_driver.perf->get_subinstance());
            const auto counter =
               &perf->cfg->categories[gid].counters[id];
            return pan_perf_counter_read_sum(perf, counter);
         });

         group.counters.push_back(cid++);

         counters.emplace_back(counter);
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
