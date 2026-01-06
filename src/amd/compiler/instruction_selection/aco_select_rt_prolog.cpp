/*
 * Copyright © 2023 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_interface.h"
#include "aco_ir.h"

namespace aco {

void
select_rt_prolog(Program* program, ac_shader_config* config,
                 const struct aco_compiler_options* options, const struct aco_shader_info* info,
                 const struct ac_shader_args* in_args, const struct ac_shader_args* out_args)
{
   init_program(program, compute_cs, info, options, config);
   Block* block = program->create_and_insert_block();
   block->kind = block_kind_top_level;
   program->workgroup_size = info->workgroup_size;
   program->wave_size = info->workgroup_size;
   calc_min_waves(program);
   Builder bld(program, block);
   block->instructions.reserve(32);
   unsigned num_sgprs = MAX2(in_args->num_sgprs_used, out_args->num_sgprs_used);
   unsigned num_vgprs = MAX2(in_args->num_vgprs_used, out_args->num_vgprs_used);

   /* Inputs:
    * Ring offsets:                s[0-1]
    * Indirect descriptor sets:    s[2]
    * Push constants pointer:      s[3]
    * Dynamic descriptors:         s[4]
    * Traversal shader address:    s[5]
    * SBT descriptors:             s[6-7]
    * Ray launch size address:     s[8-9]
    * Dynamic callable stack base: s[10]
    * Workgroup IDs (xyz):         s[11], s[12], s[13]
    * Scratch offset:              s[14]
    * Local invocation IDs:        v[0-2]
    */
   PhysReg in_ring_offsets = get_arg_reg(in_args, in_args->ring_offsets);
   PhysReg in_sbt_desc = get_arg_reg(in_args, in_args->rt.sbt_descriptors);
   PhysReg in_launch_size_addr = get_arg_reg(in_args, in_args->rt.launch_size_addr);
   PhysReg in_stack_base = get_arg_reg(in_args, in_args->rt.dynamic_callable_stack_base);
   PhysReg in_wg_id_x;
   PhysReg in_wg_id_y;
   PhysReg in_wg_id_z;
   PhysReg in_scratch_offset;
   if (options->gfx_level < GFX12) {
      in_wg_id_x = get_arg_reg(in_args, in_args->workgroup_ids[0]);
      in_wg_id_y = get_arg_reg(in_args, in_args->workgroup_ids[1]);
      in_wg_id_z = get_arg_reg(in_args, in_args->workgroup_ids[2]);
   } else {
      in_wg_id_x = PhysReg(108 + 9 /*ttmp9*/);
      in_wg_id_y = PhysReg(108 + 7 /*ttmp7*/);
   }
   if (options->gfx_level < GFX11)
      in_scratch_offset = get_arg_reg(in_args, in_args->scratch_offset);
   struct ac_arg arg_id = options->gfx_level >= GFX11 ? in_args->local_invocation_ids_packed
                                                      : in_args->local_invocation_id_x;
   PhysReg in_local_id = get_arg_reg(in_args, arg_id);

   /* Outputs:
    * Callee shader PC:            s[0-1]
    * Indirect descriptor sets:    s[2]
    * Push constants pointer:      s[3]
    * Dynamic descriptors:         s[4]
    * Traversal shader address:    s[5]
    * SBT descriptors:             s[6-7]
    * Ray launch sizes (xyz):      s[8], s[9], s[10]
    * Scratch offset (<GFX9 only): s[11]
    * Ring offsets (<GFX9 only):   s[12-13]
    * Ray launch IDs:              v[0-2]
    * Stack pointer:               v[3]
    * Shader VA:                   v[4-5]
    * Shader Record Ptr:           v[6-7]
    */
   PhysReg out_uniform_shader_addr = get_arg_reg(out_args, out_args->rt.uniform_shader_addr);
   PhysReg out_launch_size_x = get_arg_reg(out_args, out_args->rt.launch_sizes[0]);
   PhysReg out_launch_size_y = get_arg_reg(out_args, out_args->rt.launch_sizes[1]);
   PhysReg out_launch_size_z = get_arg_reg(out_args, out_args->rt.launch_sizes[2]);
   PhysReg out_launch_ids[3];
   for (unsigned i = 0; i < 3; i++)
      out_launch_ids[i] = get_arg_reg(out_args, out_args->rt.launch_ids[i]);
   PhysReg out_stack_ptr = get_arg_reg(out_args, out_args->rt.dynamic_callable_stack_base);
   PhysReg out_record_ptr = get_arg_reg(out_args, out_args->rt.shader_record);

   /* Temporaries: */
   PhysReg tmp_wg_start_x = PhysReg{num_sgprs};
   num_sgprs++;
   PhysReg tmp_wg_start_y = PhysReg{num_sgprs};
   num_sgprs++;
   PhysReg tmp_swizzle_bound_y = PhysReg{num_sgprs};
   num_sgprs++;
   PhysReg tmp_wg_id_y;
   if (program->gfx_level >= GFX12) {
      tmp_wg_id_y = PhysReg{num_sgprs};
      num_sgprs++;
   } else {
      tmp_wg_id_y = in_wg_id_y;
   }
   num_sgprs = align(num_sgprs, 2);
   PhysReg tmp_raygen_sbt = PhysReg{num_sgprs};
   num_sgprs += 2;
   PhysReg tmp_ring_offsets = PhysReg{num_sgprs};
   num_sgprs += 2;

   PhysReg tmp_swizzled_id_x = PhysReg{256 + num_vgprs++};
   PhysReg tmp_swizzled_id_y = PhysReg{256 + num_vgprs++};
   PhysReg tmp_swizzled_id_shifted_x = PhysReg{256 + num_vgprs++};
   PhysReg tmp_swizzled_id_shifted_y = PhysReg{256 + num_vgprs++};

   /* Confirm some assumptions about register aliasing */
   assert(in_ring_offsets == out_uniform_shader_addr);
   assert(get_arg_reg(in_args, in_args->push_constants) ==
          get_arg_reg(out_args, out_args->push_constants));
   assert(get_arg_reg(in_args, in_args->dynamic_descriptors) ==
          get_arg_reg(out_args, out_args->dynamic_descriptors));
   assert(get_arg_reg(in_args, in_args->rt.sbt_descriptors) ==
          get_arg_reg(out_args, out_args->rt.sbt_descriptors));
   assert(get_arg_reg(in_args, in_args->rt.traversal_shader_addr) ==
          get_arg_reg(out_args, out_args->rt.traversal_shader_addr));
   assert(in_launch_size_addr == out_launch_size_x);
   assert(in_stack_base == out_launch_size_z);
   assert(in_local_id == out_launch_ids[0]);

   /* <gfx9 reads in_scratch_offset at the end of the prolog to write out the scratch_offset
    * arg. Make sure no other outputs have overwritten it by then.
    */
   assert(options->gfx_level >= GFX9 || in_scratch_offset.reg() >= out_args->num_sgprs_used);

   /* load raygen sbt */
   bld.smem(aco_opcode::s_load_dwordx2, Definition(tmp_raygen_sbt, s2), Operand(in_sbt_desc, s2),
            Operand::c32(0u));

   /* init scratch */
   if (options->gfx_level < GFX9) {
      /* copy ring offsets to temporary location*/
      bld.sop1(aco_opcode::s_mov_b64, Definition(tmp_ring_offsets, s2),
               Operand(in_ring_offsets, s2));
   } else if (options->gfx_level < GFX11) {
      hw_init_scratch(bld, Definition(in_ring_offsets, s1), Operand(in_ring_offsets, s2),
                      Operand(in_scratch_offset, s1));
   }

   /* set stack ptr */
   bld.vop1(aco_opcode::v_mov_b32, Definition(out_stack_ptr, v1), Operand(in_stack_base, s1));

   /* load raygen address */
   bld.smem(aco_opcode::s_load_dwordx2, Definition(out_uniform_shader_addr, s2),
            Operand(tmp_raygen_sbt, s2), Operand::c32(0u));

   /* load ray launch sizes */
   assert(out_launch_size_x.reg() % 4 == 0);
   if (options->gfx_level >= GFX12) {
      bld.smem(aco_opcode::s_load_dwordx3, Definition(out_launch_size_x, s3),
               Operand(in_launch_size_addr, s2), Operand::c32(0u));
   } else {
      bld.smem(aco_opcode::s_load_dword, Definition(out_launch_size_z, s1),
               Operand(in_launch_size_addr, s2), Operand::c32(8u));
      bld.smem(aco_opcode::s_load_dwordx2, Definition(out_launch_size_x, s2),
               Operand(in_launch_size_addr, s2), Operand::c32(0u));
   }

   /* calculate ray launch ids */
   if (options->gfx_level >= GFX12) {
      bld.vop2_e64(aco_opcode::v_lshrrev_b32, Definition(out_launch_ids[2], v1), Operand::c32(16),
                   Operand(in_wg_id_y, s1));
      bld.sop2(aco_opcode::s_pack_ll_b32_b16, Definition(tmp_wg_id_y, s1), Operand(in_wg_id_y, s1),
               Operand::c32(0));
   } else {
      bld.vop1(aco_opcode::v_mov_b32, Definition(out_launch_ids[2], v1), Operand(in_wg_id_z, s1));
   }

   /* Swizzle ray launch IDs. We dispatch a 1D 32x1/64x1 workgroup natively. Many games dispatch
    * rays in a 2D grid and write RT results to an image indexed by the x/y launch ID.
    * In image space, a 1D workgroup maps to a 32/64-pixel wide line, which is inefficient for two
    * reasons:
    * - Image data is usually arranged on a Z-order curve, a long line makes for inefficient
    *   memory access patterns.
    * - Each wave working on a "line" in image space may increase divergence. It's better to trace
    *   rays in a small square, since that makes it more likely all rays hit the same or similar
    *   objects.
    *
    * It turns out arranging rays along a Z-order curve is best for both image access patterns and
    * ray divergence. Since image data is swizzled along a Z-order curve as well, swizzling the
    * launch ID should result in each lane accessing whole cachelines at once. For traced rays,
    * the Z-order curve means that each quad is arranged in a 2x2 square in image space as well.
    * Since the RT unit processes 4 lanes at a time, reducing divergence per quad may result in
    * better RT unit utilization (for example by the RT unit being able to skip the quad entirely
    * if all 4 lanes are inactive).
    *
    * To swizzle along a Z-order curve, treat the 1D lane ID as a morton code. Then, do the inverse
    * of morton code generation (i.e. deinterleaving the bits) to recover the x-y
    * coordinates on the Z-order curve.
    */

   /* Deinterleave bits - odd bits go to tmp_swizzled_id_x, even ones to tmp_swizzled_id_y */
   bld.vop2(aco_opcode::v_and_b32, Definition(tmp_swizzled_id_x, v1), Operand::c32(0x55),
            Operand(in_local_id, v1));
   bld.vop2(aco_opcode::v_and_b32, Definition(tmp_swizzled_id_y, v1), Operand::c32(0xaa),
            Operand(in_local_id, v1));
   bld.vop2(aco_opcode::v_lshrrev_b32, Definition(tmp_swizzled_id_y, v1), Operand::c32(1),
            Operand(tmp_swizzled_id_y, v1));

   /* The deinterleaved bits are currently padded with a zero between each bit, like so:
    * 0 A 0 B 0 C 0 D
    * Compact the deinterleaved bits by factor 2 to remove the padding, resulting in
    * A B C D
    */
   bld.vop2(aco_opcode::v_lshrrev_b32, Definition(tmp_swizzled_id_shifted_x, v1), Operand::c32(1),
            Operand(tmp_swizzled_id_x, v1));
   bld.vop2(aco_opcode::v_lshrrev_b32, Definition(tmp_swizzled_id_shifted_y, v1), Operand::c32(1),
            Operand(tmp_swizzled_id_y, v1));
   bld.vop2(aco_opcode::v_or_b32, Definition(tmp_swizzled_id_x, v1), Operand(tmp_swizzled_id_x, v1),
            Operand(tmp_swizzled_id_shifted_x, v1));
   bld.vop2(aco_opcode::v_or_b32, Definition(tmp_swizzled_id_y, v1), Operand(tmp_swizzled_id_y, v1),
            Operand(tmp_swizzled_id_shifted_y, v1));
   bld.vop2(aco_opcode::v_and_b32, Definition(tmp_swizzled_id_x, v1), Operand::c32(0x33u),
            Operand(tmp_swizzled_id_x, v1));
   bld.vop2(aco_opcode::v_and_b32, Definition(tmp_swizzled_id_y, v1), Operand::c32(0x33u),
            Operand(tmp_swizzled_id_y, v1));

   bld.vop2(aco_opcode::v_lshrrev_b32, Definition(tmp_swizzled_id_shifted_x, v1), Operand::c32(2),
            Operand(tmp_swizzled_id_x, v1));
   bld.vop2(aco_opcode::v_lshrrev_b32, Definition(tmp_swizzled_id_shifted_y, v1), Operand::c32(2),
            Operand(tmp_swizzled_id_y, v1));
   bld.vop2(aco_opcode::v_or_b32, Definition(tmp_swizzled_id_x, v1), Operand(tmp_swizzled_id_x, v1),
            Operand(tmp_swizzled_id_shifted_x, v1));
   bld.vop2(aco_opcode::v_or_b32, Definition(tmp_swizzled_id_y, v1), Operand(tmp_swizzled_id_y, v1),
            Operand(tmp_swizzled_id_shifted_y, v1));
   bld.vop2(aco_opcode::v_and_b32, Definition(tmp_swizzled_id_x, v1), Operand::c32(0x0Fu),
            Operand(tmp_swizzled_id_x, v1));
   bld.vop2(aco_opcode::v_and_b32, Definition(tmp_swizzled_id_y, v1), Operand::c32(0x0Fu),
            Operand(tmp_swizzled_id_y, v1));

   /* Fix up the workgroup IDs after converting from 32x1/64x1 to 8x4/8x8. The X dimension of the
    * workgroup size gets divided by 4/8, while the Y dimension gets multiplied by the same amount.
    * Rearrange the workgroups to make up for that, by rounding the Y component of the workgroup ID
    * to the nearest multiple of 4/8. The remainder gets added to the X dimension, to make up for
    * the fact we divided the X component of the ID.
    */
   uint32_t workgroup_size_log2 = util_logbase2(program->workgroup_size);
   bld.sop2(aco_opcode::s_lshl_b32, Definition(tmp_wg_start_x, s1), Definition(scc, s1),
            Operand(in_wg_id_x, s1), Operand::c32(workgroup_size_log2));

   /* unsigned y_remainder = tmp_wg_id_y % wg_height
    * We use tmp_wg_start_y to store y_rem, and overwrite it later with the real wg_start_y.
    */
   uint32_t workgroup_width_log2 = 3u;
   uint32_t workgroup_height_mask = program->workgroup_size == 32 ? 0x3u : 0x7u;
   bld.sop2(aco_opcode::s_and_b32, Definition(tmp_wg_start_y, s1), Definition(scc, s1),
            Operand(tmp_wg_id_y, s1), Operand::c32(workgroup_height_mask));
   /* wg_start_x += y_remainder * workgroup_width (workgroup_width == 8) */
   bld.sop2(aco_opcode::s_lshl_b32, Definition(tmp_wg_start_y, s1), Definition(scc, s1),
            Operand(tmp_wg_start_y, s1), Operand::c32(workgroup_width_log2));
   bld.sop2(aco_opcode::s_add_u32, Definition(tmp_wg_start_x, s1), Definition(scc, s1),
            Operand(tmp_wg_start_x, s1), Operand(tmp_wg_start_y, s1));
   /* wg_start_y = ROUND_DOWN_TO(in_wg_y, workgroup_height) */
   bld.sop2(aco_opcode::s_and_b32, Definition(tmp_wg_start_y, s1), Definition(scc, s1),
            Operand(tmp_wg_id_y, s1), Operand::c32(~workgroup_height_mask));

   bld.vop2(aco_opcode::v_add_u32, Definition(tmp_swizzled_id_x, v1), Operand(tmp_wg_start_x, s1),
            Operand(tmp_swizzled_id_x, v1));
   bld.vop2(aco_opcode::v_add_u32, Definition(tmp_swizzled_id_y, v1), Operand(tmp_wg_start_y, s1),
            Operand(tmp_swizzled_id_y, v1));

   /* We can only swizzle launch IDs if we run a full workgroup, and the resulting launch IDs
    * won't exceed the launch size. Calculate unswizzled launch IDs here to fall back to them
    * if the swizzled launch IDs are out of bounds.
    */
   bld.vop1(aco_opcode::v_mov_b32, Definition(out_launch_ids[1], v1), Operand(tmp_wg_id_y, s1));
   bld.vop3(aco_opcode::v_mad_u32_u24, Definition(out_launch_ids[0], v1), Operand(in_wg_id_x, s1),
            Operand::c32(program->workgroup_size), Operand(in_local_id, v1));

   /* Round the launch size down to the nearest multiple of workgroup_height. If the workgroup ID
    * exceeds this, then the swizzled IDs' Y component will exceed the Y launch size and we have to
    * fall back to unswizzled IDs.
    */
   bld.sop2(aco_opcode::s_and_b32, Definition(tmp_swizzle_bound_y, s1), Definition(scc, s1),
            Operand(out_launch_size_y, s1), Operand::c32(~workgroup_height_mask));
   /* If we are only running a partial workgroup, swizzling would yield a wrong result. */
   if (program->gfx_level >= GFX8) {
      bld.sopc(Builder::s_cmp_lg, Definition(scc, s1), Operand(exec, bld.lm),
               Operand::c32_or_c64(-1u, program->workgroup_size == 64));
   } else {
      /* Write the XOR result to vcc because it's currently unused and a convenient register (always
       * the same size as exec). We only care about the value of scc, i.e. if the result is nonzero
       * (vcc is about to be overwritten anyway).
       */
      bld.sop2(Builder::s_xor, Definition(vcc, bld.lm), Definition(scc, s1), Operand(exec, bld.lm),
               Operand::c32_or_c64(-1u, program->workgroup_size == 64));
   }
   bld.sop2(Builder::s_cselect, Definition(vcc, bld.lm),
            Operand::c32_or_c64(-1u, program->wave_size == 64),
            Operand::c32_or_c64(0u, program->wave_size == 64), Operand(scc, s1));
   bld.sopc(aco_opcode::s_cmp_ge_u32, Definition(scc, s1), Operand(tmp_wg_id_y, s1),
            Operand(tmp_swizzle_bound_y, s1));
   bld.sop2(Builder::s_cselect, Definition(vcc, bld.lm),
            Operand::c32_or_c64(-1u, program->wave_size == 64),
            Operand(vcc, bld.lm), Operand(scc, s1));

   bld.vop2(aco_opcode::v_cndmask_b32, Definition(out_launch_ids[0], v1),
            Operand(tmp_swizzled_id_x, v1), Operand(out_launch_ids[0], v1), Operand(vcc, bld.lm));
   bld.vop2(aco_opcode::v_cndmask_b32, Definition(out_launch_ids[1], v1),
            Operand(tmp_swizzled_id_y, v1), Operand(out_launch_ids[1], v1), Operand(vcc, bld.lm));

   /* calculate shader record ptr: SBT + RADV_RT_HANDLE_SIZE */
   if (options->gfx_level < GFX9) {
      bld.vop2_e64(aco_opcode::v_add_co_u32, Definition(out_record_ptr, v1), Definition(vcc, s2),
                   Operand(tmp_raygen_sbt, s1), Operand::c32(32u));
   } else {
      bld.vop2_e64(aco_opcode::v_add_u32, Definition(out_record_ptr, v1),
                   Operand(tmp_raygen_sbt, s1), Operand::c32(32u));
   }
   bld.vop1(aco_opcode::v_mov_b32, Definition(out_record_ptr.advance(4), v1),
            Operand(tmp_raygen_sbt.advance(4), s1));

   if (options->gfx_level < GFX9) {
      /* write scratch/ring offsets to outputs, if needed */
      bld.sop1(aco_opcode::s_mov_b32,
               Definition(get_arg_reg(out_args, out_args->scratch_offset), s1),
               Operand(in_scratch_offset, s1));
      bld.sop1(aco_opcode::s_mov_b64, Definition(get_arg_reg(out_args, out_args->ring_offsets), s2),
               Operand(tmp_ring_offsets, s2));
   }

   /* jump to raygen */
   bld.sop1(aco_opcode::s_setpc_b64, Operand(out_uniform_shader_addr, s2));

   program->config->float_mode = program->blocks[0].fp_mode.val;
   program->config->num_vgprs = get_vgpr_alloc(program, num_vgprs);
   program->config->num_sgprs = get_sgpr_alloc(program, num_sgprs);
}

} // namespace aco
