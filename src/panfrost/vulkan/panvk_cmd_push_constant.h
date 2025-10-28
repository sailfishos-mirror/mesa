/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_PUSH_CONSTANT_H
#define PANVK_CMD_PUSH_CONSTANT_H

#include <stdint.h>

#include "genxml/gen_macros.h"

struct panvk_cmd_buffer;
struct panvk_shader_variant;

#define MAX_PUSH_CONSTANTS_SIZE 256

struct panvk_push_constant_state {
   uint64_t data[MAX_PUSH_CONSTANTS_SIZE / sizeof(uint64_t)];
};

VkResult panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *shader,
   uint64_t *push_ptr, uint32_t repeat_count);

VkResult panvk_per_arch(cmd_prepare_compute_push_uniforms)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *shader,
   uint64_t *push_ptr);

#endif
