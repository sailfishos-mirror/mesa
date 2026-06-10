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

/* After nir_lower_int_to_float, integer ALU is represented as float ALU, but
 * intrinsics keep their original integer input/output contracts. Cast UBO
 * sources back to integer, and draw system values back to float for their users.
 */
static bool
no_integer_intrinsic_fixup(nir_builder *b, nir_intrinsic_instr *intr,
                           void *data)
{
   switch (intr->intrinsic) {
   /* draw_create_vs_llvm() can lower uniforms with load_vec4=false before
    * gallivm sees the shader, so no_integers shaders can already contain
    * load_ubo in addition to load_ubo_vec4.
    */
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_vec4:
      b->cursor = nir_before_instr(&intr->instr);

      /* nir_lower_int_to_float converts only constants whose SSA value is
       * typed as integer by nir_gather_types(). For load_ubo / load_ubo_vec4,
       * NIR marks src[1] as nir_type_int so constant offsets are numerically
       * converted to float and must be repaired with f2i32. src[0] is the UBO
       * buffer index and is not typed, so constant buffer indices keep their
       * raw integer payload and must not get f2i32.
       */
       if (!nir_src_is_const(intr->src[0]))
         nir_src_rewrite(&intr->src[0], nir_f2i32(b, intr->src[0].ssa));

       nir_src_rewrite(&intr->src[1], nir_f2i32(b, intr->src[1].ssa));
      return true;
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_vertex_id_zero_base:
   case nir_intrinsic_load_first_vertex:
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_draw_id:
      b->cursor = nir_after_instr(&intr->instr);
      nir_def *as_float = nir_i2f32(b, &intr->def);
      nir_def_rewrite_uses_after(&intr->def, as_float);
      return true;
   default:
      return false;
   }

}

bool
lp_nir_no_integer_intrinsic_fixup(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, no_integer_intrinsic_fixup,
                                     nir_metadata_control_flow, NULL);
}
