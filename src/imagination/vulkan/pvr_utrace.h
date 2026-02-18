/*
 * Copyright © 2026 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_UTRACE_H
#define PVR_UTRACE_H

#include <stdint.h>

#include "util/perf/u_trace.h"
#include "ds/pvr_driver_ds.h"
#include "ds/pvr_tracepoints.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pvr_device;
struct pvr_queue;
struct pvr_cmd_buffer;

/* Initialize u_trace support for device */
void pvr_device_utrace_init(struct pvr_device *device);

/* Cleanup u_trace support */
void pvr_device_utrace_finish(struct pvr_device *device);

/* Initialize command buffer trace */
void pvr_cmd_buffer_utrace_init(struct pvr_cmd_buffer *cmd_buffer);

/* Cleanup command buffer trace */
void pvr_cmd_buffer_utrace_fini(struct pvr_cmd_buffer *cmd_buffer);

#ifdef __cplusplus
}
#endif

#endif /* PVR_UTRACE_H */
