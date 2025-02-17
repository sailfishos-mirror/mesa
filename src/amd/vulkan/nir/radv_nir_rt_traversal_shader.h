/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_NIR_RT_TRAVERSAL_SHADER_H
#define RADV_NIR_RT_TRAVERSAL_SHADER_H

#include "radv_pipeline_rt.h"

typedef void (*radv_nir_traversal_preprocess_cb)(nir_shader *nir);

nir_shader *radv_build_traversal_shader(struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline,
                                        struct radv_ray_tracing_stage_info *info,
                                        radv_nir_traversal_preprocess_cb preprocess);

#endif // RADV_NIR_RT_TRAVERSAL_SHADER_H
