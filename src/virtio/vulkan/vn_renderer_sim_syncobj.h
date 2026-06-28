/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VN_RENDERER_SIM_SYNCOBJ_H
#define VN_RENDERER_SIM_SYNCOBJ_H

#include <stdbool.h>
#include <stdint.h>

struct vn_renderer_wait;

uint32_t
sim_syncobj_create(bool signaled);

void
sim_syncobj_destroy(uint32_t syncobj_handle);

int
sim_syncobj_submit(uint32_t syncobj_handle, int sync_fd, uint64_t point);

int
sim_syncobj_reset(uint32_t syncobj_handle);

int
sim_syncobj_query(uint32_t syncobj_handle, uint64_t *point);

int
sim_syncobj_signal(uint32_t syncobj_handle, uint64_t point);

int
sim_syncobj_wait(const struct vn_renderer_wait *wait);

int
sim_syncobj_export(uint32_t syncobj_handle);

uint32_t
sim_syncobj_import(uint32_t syncobj_handle, int fd);

#endif /* VN_RENDERER_SIM_SYNCOBJ_H */
