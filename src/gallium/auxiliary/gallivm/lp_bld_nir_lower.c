/*
 * Copyright 2026 Pavel Ondračka <pavel.ondracka@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "lp_bld_nir.h"

#include "nir_builder.h"

static bool
lower_ubo_vec4_instr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_ubo_vec4)
      return false;

   unsigned base = nir_intrinsic_base(intr);
   unsigned component = nir_intrinsic_component(intr);

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *buf_idx = intr->src[0].ssa;
   unsigned byte_base = base * 16 + component * 4;
   nir_def *byte_offset =
      nir_iadd_imm(b, nir_imul_imm(b, intr->src[1].ssa, 16), byte_base);

   nir_def *result = nir_load_ubo(b, intr->def.num_components, 32,
                                  buf_idx, byte_offset,
                                  .access = nir_intrinsic_access(intr),
                                  .align_mul = 16,
                                  .align_offset = component * 4,
                                  .range_base = 0,
                                  .range = ~0u);

   nir_def_replace(&intr->def, result);
   return true;
}

bool
lp_nir_lower_ubo_vec4(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, lower_ubo_vec4_instr,
                                     nir_metadata_control_flow, NULL);
}
