/*
 * Copyright © 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_pps_perf.h"

#include <lib/kmod/pan_kmod.h>
#include <perf/pan_perf.h>

#include <pps/pps.h>
#include <util/ralloc.h>

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
PanfrostPerf::enable() const
{
   assert(perf);
   return pan_perf_enable(perf);
}

void
PanfrostPerf::disable() const
{
   assert(perf);
   pan_perf_disable(perf);
}

int
PanfrostPerf::dump() const
{
   assert(perf);
   return pan_perf_dump(perf);
}

} // namespace pps
