/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "sfn_fill_bytecode.h"
#include "../eg_sq.h"

#include <cassert>
#include <map>
#include <optional>

#include "sfn_instr_export.h"
#include "sfn_instr_fetch.h"
#include "sfn_instr_mem.h"
#include "sfn_instr_tex.h"
#include "sfn_debug.h"

namespace r600 {

extern const std::map<ESDOp, int> ds_opcode_map;

static r600_bytecode_tex
fill_bytecode_tex(const TexInstr& tex_instr)
{
   r600_bytecode_tex tex = {};
   tex.op = tex_instr.opcode();
   tex.sampler_id = tex_instr.sampler_id();
   tex.resource_id = tex_instr.resource_id();
   tex.src_gpr = tex_instr.src().sel();
   tex.dst_gpr = tex_instr.dst().sel();
   tex.dst_sel_x = tex_instr.dest_swizzle(0);
   tex.dst_sel_y = tex_instr.dest_swizzle(1);
   tex.dst_sel_z = tex_instr.dest_swizzle(2);
   tex.dst_sel_w = tex_instr.dest_swizzle(3);
   tex.src_sel_x = tex_instr.src()[0]->chan();
   tex.src_sel_y = tex_instr.src()[1]->chan();
   tex.src_sel_z = tex_instr.src()[2]->chan();
   tex.src_sel_w = tex_instr.src()[3]->chan();
   tex.coord_type_x = !tex_instr.has_tex_flag(TexInstr::x_unnormalized);
   tex.coord_type_y = !tex_instr.has_tex_flag(TexInstr::y_unnormalized);
   tex.coord_type_z = !tex_instr.has_tex_flag(TexInstr::z_unnormalized);
   tex.coord_type_w = !tex_instr.has_tex_flag(TexInstr::w_unnormalized);
   tex.offset_x = tex_instr.get_offset(0);
   tex.offset_y = tex_instr.get_offset(1);
   tex.offset_z = tex_instr.get_offset(2);
   tex.resource_index_mode = tex_instr.resource_index_mode();
   tex.sampler_index_mode = tex_instr.sampler_index_mode();

   if (tex_instr.opcode() == TexInstr::get_gradient_h ||
       tex_instr.opcode() == TexInstr::get_gradient_v)
      tex.inst_mod = tex_instr.has_tex_flag(TexInstr::grad_fine) ? 1 : 0;
   else
      tex.inst_mod = tex_instr.inst_mode();

   return tex;
}

static r600_bytecode_output
fill_bytecode_export_alpha_to_coverage(const ExportInstr& export_instr)
{
   const auto& value = export_instr.value();

   r600_bytecode_output output = {};
   output.gpr = value.sel();
   output.elem_size = 3;
   output.swizzle_x = 7;
   output.swizzle_y = 7;
   output.swizzle_z = 7;
   output.swizzle_w = value[3]->chan();
   output.array_base = 61;
   output.burst_count = 1;
   output.op = CF_OP_EXPORT;
   output.type = export_instr.export_type();

   return output;
}

static std::optional<r600_bytecode_output>
fill_bytecode_export(const ExportInstr& export_instr,
                     bool ps_alpha_to_one)
{
   const auto& value = export_instr.value();

   r600_bytecode_output output = {};
   output.gpr = value.sel();
   output.elem_size = 3;
   output.swizzle_x = value[0]->chan();
   output.swizzle_y = value[1]->chan();
   output.swizzle_z = value[2]->chan();
   output.burst_count = 1;
   output.op = export_instr.is_last_export() ? CF_OP_EXPORT_DONE : CF_OP_EXPORT;
   output.type = export_instr.export_type();

   switch (export_instr.export_type()) {
   case ExportInstr::pixel:
      output.swizzle_w = ps_alpha_to_one ? 5 : value[3]->chan();
      output.array_base = export_instr.location();
      break;
   case ExportInstr::pos:
      output.swizzle_w = value[3]->chan();
      output.array_base = 60 + export_instr.location();
      break;
   case ExportInstr::param:
      output.swizzle_w = value[3]->chan();
      output.array_base = export_instr.location();
      break;
   default:
      return std::nullopt;
   }

   if (output.swizzle_x > 3 && output.swizzle_y > 3 && output.swizzle_z > 3 &&
       output.swizzle_w > 3)
      output.gpr = 0;

   return output;
}

static r600_bytecode_output
fill_bytecode_scratch(const ScratchIOInstr& instr,
                      enum amd_gfx_level gfx_level)
{
   r600_bytecode_output output = {};
   output.op = CF_OP_MEM_SCRATCH;
   output.elem_size = 3;
   output.gpr = instr.value().sel();
   output.mark = !instr.is_read();
   output.comp_mask = instr.is_read() ? 0xf : instr.write_mask();
   output.swizzle_x = 0;
   output.swizzle_y = 1;
   output.swizzle_z = 2;
   output.swizzle_w = 3;
   output.burst_count = 1;

   assert(!instr.is_read() || gfx_level < R700);

   if (instr.address()) {
      output.type = instr.is_read() || gfx_level > R600 ? 3 : 1;
      output.index_gpr = instr.address()->sel();
      output.array_size = instr.array_size();
   } else {
      output.type = instr.is_read() || gfx_level > R600 ? 2 : 0;
      output.array_base = instr.location();
   }

   return output;
}

static r600_bytecode_output
fill_bytecode_stream_out(const StreamOutInstr& instr,
                         enum amd_gfx_level gfx_level)
{
   r600_bytecode_output output = {};
   output.gpr = instr.value().sel();
   output.elem_size = instr.element_size();
   output.array_base = instr.array_base();
   output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE;
   output.burst_count = instr.burst_count();
   output.array_size = instr.array_size();
   output.comp_mask = instr.comp_mask();
   output.op = instr.op(gfx_level);

   return output;
}

static r600_bytecode_output
fill_bytecode_mem_ring(const MemRingOutInstr& instr)
{
   r600_bytecode_output output = {};
   output.gpr = instr.value().sel();
   output.type = instr.type();
   output.elem_size = 3;
   output.comp_mask = 0xf;
   output.burst_count = 1;
   output.op = instr.op();
   if (instr.type() == MemRingOutInstr::mem_write_ind ||
       instr.type() == MemRingOutInstr::mem_write_ind_ack) {
      output.index_gpr = instr.index_reg();
      output.array_size = 0xfff;
   }
   output.array_base = instr.array_base();

   return output;
}

static r600_bytecode_vtx
fill_bytecode_fetch(const FetchInstr& fetch_instr)
{
   r600_bytecode_vtx vtx = {};
   vtx.op = fetch_instr.opcode();
   vtx.buffer_id = fetch_instr.resource_id();
   vtx.fetch_type = fetch_instr.fetch_type();
   vtx.src_gpr = fetch_instr.src().sel();
   vtx.src_sel_x = fetch_instr.src().chan();
   vtx.mega_fetch_count = fetch_instr.mega_fetch_count();
   vtx.dst_gpr = fetch_instr.dst().sel();
   vtx.dst_sel_x = fetch_instr.dest_swizzle(0);
   vtx.dst_sel_y = fetch_instr.dest_swizzle(1);
   vtx.dst_sel_z = fetch_instr.dest_swizzle(2);
   vtx.dst_sel_w = fetch_instr.dest_swizzle(3);
   vtx.use_const_fields = fetch_instr.has_fetch_flag(FetchInstr::use_const_field);
   vtx.data_format = fetch_instr.data_format();
   vtx.num_format_all = fetch_instr.num_format();
   vtx.format_comp_all = fetch_instr.has_fetch_flag(FetchInstr::format_comp_signed);
   vtx.endian = fetch_instr.endian_swap();
   vtx.buffer_index_mode = fetch_instr.resource_index_mode();
   vtx.offset = fetch_instr.src_offset();
   vtx.indexed = fetch_instr.has_fetch_flag(FetchInstr::indexed);
   vtx.uncached = fetch_instr.has_fetch_flag(FetchInstr::uncached);
   vtx.elem_size = fetch_instr.elm_size();
   vtx.array_base = fetch_instr.array_base();
   vtx.array_size = fetch_instr.array_size();
   vtx.srf_mode_all = fetch_instr.has_fetch_flag(FetchInstr::srf_mode);

   return vtx;
}

static r600_bytecode_gds
fill_bytecode_gds(const GDSInstr& instr,
                  enum amd_gfx_level gfx_level)
{
   r600_bytecode_gds gds = {};
   gds.op = ds_opcode_map.at(instr.opcode());
   gds.uav_id = instr.resource_id();
   gds.uav_index_mode = instr.resource_index_mode();
   gds.src_gpr = instr.src().sel();

   gds.src_sel_x = instr.src()[0]->chan() < 7 ? instr.src()[0]->chan() : 4;
   gds.src_sel_y = instr.src()[1]->chan() < 7 ? instr.src()[1]->chan() : 4;
   gds.src_sel_z = instr.src()[2]->chan() < 7 ? instr.src()[2]->chan() : 4;

   gds.dst_sel_x = 7;
   gds.dst_sel_y = 7;
   gds.dst_sel_z = 7;
   gds.dst_sel_w = 7;

   if (instr.dest()) {
      gds.dst_gpr = instr.dest()->sel();
      switch (instr.dest()->chan()) {
      case 0:
         gds.dst_sel_x = 0;
         break;
      case 1:
         gds.dst_sel_y = 0;
         break;
      case 2:
         gds.dst_sel_z = 0;
         break;
      case 3:
         gds.dst_sel_w = 0;
      }
   }

   gds.src_gpr2 = 0;
   gds.alloc_consume = gfx_level < CAYMAN ? 1 : 0;

   return gds;
}

static r600_bytecode_gds
fill_bytecode_tf_write(const WriteTFInstr& instr,
                       unsigned start_chan)
{
   assert(start_chan == 0 || start_chan == 2);

   const auto& value = instr.value();

   r600_bytecode_gds gds = {};
   gds.src_gpr = value.sel();
   gds.src_sel_x = value[start_chan]->chan();
   gds.src_sel_y = value[start_chan + 1]->chan();
   gds.src_sel_z = 4;
   gds.dst_sel_x = 7;
   gds.dst_sel_y = 7;
   gds.dst_sel_z = 7;
   gds.dst_sel_w = 7;
   gds.op = FETCH_OP_TF_WRITE;

   return gds;
}

bool
emit_bytecode_tex(r600_bytecode& bc, const TexInstr& tex_instr)
{
   r600_bytecode_tex tex = fill_bytecode_tex(tex_instr);
   if (r600_bytecode_add_tex(&bc, &tex)) {
      R600_ASM_ERR("shader_from_nir: Error creating tex assembly instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_export_alpha_to_coverage(r600_bytecode& bc, const ExportInstr& export_instr)
{
   r600_bytecode_output output = fill_bytecode_export_alpha_to_coverage(export_instr);

   if (r600_bytecode_add_output(&bc, &output)) {
      R600_ASM_ERR("Error adding export at location %d\n", output.array_base);
      return false;
   }

   return true;
}

bool
emit_bytecode_export(r600_bytecode& bc,
                     const ExportInstr& export_instr,
                     bool ps_alpha_to_one)
{
   auto output_opt = fill_bytecode_export(export_instr, ps_alpha_to_one);
   if (!output_opt) {
      R600_ASM_ERR("shader_from_nir: export %d type not yet supported\n",
                   export_instr.export_type());
      return false;
   }
   
   if (r600_bytecode_add_output(&bc, &output_opt.value())) {
      R600_ASM_ERR("Error adding export at location %d\n", export_instr.location());
      return false;
   }

   return true;
}

bool
emit_bytecode_scratch(r600_bytecode& bc, const ScratchIOInstr& instr)
{
   r600_bytecode_output output = fill_bytecode_scratch(instr, bc.gfx_level);

   if (r600_bytecode_add_output(&bc, &output)) {
      R600_ASM_ERR("shader_from_nir: Error creating SCRATCH_WR assembly instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_stream_out(r600_bytecode& bc,
                         const StreamOutInstr& instr)
{
   r600_bytecode_output output = fill_bytecode_stream_out(instr, bc.gfx_level);

   if (r600_bytecode_add_output(&bc, &output)) {
      R600_ASM_ERR("shader_from_nir: Error creating stream output instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_mem_ring(r600_bytecode& bc, const MemRingOutInstr& instr)
{
   r600_bytecode_output output = fill_bytecode_mem_ring(instr);

   if (r600_bytecode_add_output(&bc, &output)) {
      R600_ASM_ERR("shader_from_nir: Error creating mem ring write instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_fetch(r600_bytecode& bc, const FetchInstr& fetch_instr, bool use_tc)
{
   r600_bytecode_vtx vtx = fill_bytecode_fetch(fetch_instr);

   int r = use_tc ? r600_bytecode_add_vtx_tc(&bc, &vtx) : r600_bytecode_add_vtx(&bc, &vtx);
   if (r) {
      R600_ASM_ERR("shader_from_nir: Error creating tex assembly instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_gds(r600_bytecode& bc, const GDSInstr& instr)
{
   r600_bytecode_gds gds = fill_bytecode_gds(instr, bc.gfx_level);

   if (r600_bytecode_add_gds(&bc, &gds)) {
      R600_ASM_ERR("shader_from_nir: Error creating GDS instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_tf_write(r600_bytecode& bc, const WriteTFInstr& instr)
{
   r600_bytecode_gds gds = fill_bytecode_tf_write(instr, 0);
   if (r600_bytecode_add_gds(&bc, &gds) != 0) {
      R600_ASM_ERR("shader_from_nir: Error creating transform feedback write instruction\n");
      return false;
   }

   if (instr.value()[2]->chan() != 7) {
      gds = fill_bytecode_tf_write(instr, 2);
      if (r600_bytecode_add_gds(&bc, &gds) != 0) {
         R600_ASM_ERR("shader_from_nir: Error creating transform feedback write instruction\n");
         return false;
      }
   }
   return true;
}

void
fill_bytecode_rat(r600_bytecode_cf& cf, const RatInstr& instr,
                  unsigned rat_base, unsigned shader_type)
{
   int rat_idx = instr.resource_id();

   cf.rat.id = rat_idx + rat_base;
   cf.rat.inst = instr.rat_op();
   cf.rat.index_mode = instr.resource_index_mode();
   cf.output.type = instr.need_ack() ? 3 : 1;
   cf.output.gpr = instr.data_gpr();
   cf.output.index_gpr = instr.index_gpr();
   cf.output.comp_mask = instr.comp_mask();
   cf.output.burst_count = instr.burst_count();
   assert(instr.data_swz(0) == PIPE_SWIZZLE_X);
   if (cf.rat.inst != RatInstr::STORE_TYPED) {
      assert(instr.data_swz(1) == PIPE_SWIZZLE_Y ||
             instr.data_swz(1) == PIPE_SWIZZLE_MAX);
      assert(instr.data_swz(2) == PIPE_SWIZZLE_Z ||
             instr.data_swz(2) == PIPE_SWIZZLE_MAX);
   }

   cf.vpm = shader_type == MESA_SHADER_FRAGMENT;
   cf.barrier = 1;
   cf.mark = instr.need_ack();
   cf.output.elem_size = instr.elm_size();
}

} // namespace r600