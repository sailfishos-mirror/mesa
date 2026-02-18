/*
 * Copyright © 2026 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_DRIVER_DS_H
#define PVR_DRIVER_DS_H

#include "util/macros.h"
#include "util/perf/u_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

enum pvr_ds_tracepoint_flags {
   /**
    * Whether the tracepoint's timestamp must be recorded with an
    * end-of-pipe timestamp.
    */
   PVR_DS_TRACEPOINT_FLAG_END_OF_PIPE = BITFIELD_BIT(0),
   /**
    * Whether this tracepoint's timestamp is recorded on the compute pipeline.
    */
   PVR_DS_TRACEPOINT_FLAG_END_CS = BITFIELD_BIT(1),
   /**
    * Whether this tracepoint doesn't generate a timestamp but instead repeats
    * the last one.
    */
   PVR_DS_TRACEPOINT_FLAG_REPEAT_LAST = BITFIELD_BIT(2),
};

void pvr_driver_ds_init(void);

struct pvr_ds_device {
   struct u_trace_context trace_context;
};

void pvr_ds_device_fini(struct pvr_ds_device *device);

#ifdef __cplusplus
}
#endif

#endif /* PVR_DRIVER_DS_H */
