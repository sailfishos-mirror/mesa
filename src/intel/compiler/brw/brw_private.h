/* -*- c++ -*- */
/*
 * Copyright © 2021 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "brw_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* brw_reg_allocate.cpp */
void brw_alloc_reg_sets(struct brw_compiler *compiler);

/* brw_disasm.c */
extern const char *const conditional_modifier[16];
extern const char *const pred_ctrl_align16[16];

#ifndef NDEBUG
void brw_debug_archive_nir(debug_archiver *archiver, nir_shader *nir,
                           unsigned dispatch_width, const char *step);
#else
static inline void
brw_debug_archive_nir(debug_archiver *archiver, nir_shader *nir,
                      unsigned dispatch_width, const char *step) {}
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <variant>

unsigned brw_required_dispatch_width(const struct shader_info *info);

static constexpr int SIMD_COUNT = 3;

struct brw_simd_selection_state {
   const struct intel_device_info *devinfo;

   std::variant<struct brw_cs_prog_data *,
                struct brw_bs_prog_data *> prog_data;

   unsigned required_width;

   const char *error[SIMD_COUNT];

   bool compiled[SIMD_COUNT];
   bool spilled[SIMD_COUNT];
   bool beyond_threshold[SIMD_COUNT];
};

inline int brw_simd_first_compiled(const brw_simd_selection_state &state)
{
   for (int i = 0; i < SIMD_COUNT; i++) {
      if (state.compiled[i])
         return i;
   }
   return -1;
}

inline bool brw_simd_any_compiled(const brw_simd_selection_state &state)
{
   return brw_simd_first_compiled(state) >= 0;
}

unsigned brw_geometry_stage_dispatch_width(const struct intel_device_info *devinfo);

bool brw_simd_should_compile(brw_simd_selection_state &state, unsigned simd);

void brw_simd_mark_compiled(brw_simd_selection_state &state, unsigned simd, bool spilled);

int brw_simd_select(const brw_simd_selection_state &state);

int brw_simd_select_for_workgroup_size(const struct intel_device_info *devinfo,
                                       const struct brw_cs_prog_data *prog_data,
                                       const unsigned *sizes);

bool brw_should_print_shader(const nir_shader *shader, uint64_t debug_flag, uint32_t source_hash);

#endif // __cplusplus
