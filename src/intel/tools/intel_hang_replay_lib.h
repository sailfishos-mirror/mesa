/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <assert.h>
#include <unistd.h>

#include "common/intel_hang_dump.h"
#include "util/u_dynarray.h"

struct gem_bo {
   off_t file_offset;
   uint32_t gem_handle;
   uint64_t offset;
   uint64_t size;
   bool hw_img;
   struct intel_hang_dump_block_vm_properties props;
};

struct gem_bo *intel_hang_replay_find_bo(struct util_dynarray *buffers,
                                         uint64_t address);

int compare_bos(const void *b1, const void *b2);
void skip_data(int file_fd, size_t size);
void write_malloc_data(void *out_data, int file_fd, size_t size);
