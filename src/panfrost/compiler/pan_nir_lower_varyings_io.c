/*
 * Copyright (C) 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "nir_builder.h"

#include "panfrost/model/pan_model.h"

struct lower_vs_outputs_ctx {
   unsigned arch;
   const struct pan_varying_layout *varying_layout;
   bool has_idvs;
   bool has_extended_fifo;
};

static void
build_attr_buf_write(struct nir_builder *b, nir_def *data,
                     const struct pan_varying_slot *slot, uint32_t view_index,
                     const struct lower_vs_outputs_ctx *ctx)
{
   /* We need the precise memory layout */
   pan_varying_layout_require_layout(ctx->varying_layout);

   nir_def *index = nir_load_idvs_output_buf_index_pan(b);

   uint32_t res, view_stride;
   if (slot->section == PAN_VARYING_SECTION_GENERIC) {
      /* The varying buffer is bound at index 1 on v12+ */
      uint32_t res_index = ctx->arch >= 12 ? 1 : 0;
      res = pan_res_handle(61, res_index);
      view_stride = 4;
   } else {
      res = pan_res_handle(61, 0);
      view_stride = ctx->has_extended_fifo ? 8 : 4;
   }

   uint32_t index_offset = view_index * view_stride;
   if (slot->section == PAN_VARYING_SECTION_ATTRIBS)
      index_offset += 4;

   /* v9+ cache hints, generic varyings don't need caching while
    * position/attribute varyings are reused by other units inside of the GPU.
    * TODO: Do we really want ESTREAM on generic varyings?
    */
   enum gl_access_qualifier access =
      slot->section == PAN_VARYING_SECTION_GENERIC ? ACCESS_ESTREAM_PAN :
                                                     ACCESS_ISTREAM_PAN;

   index = nir_iadd_imm(b, index, index_offset);
   nir_def *addr = nir_lea_buf_pan(b, nir_imm_int(b, res), index);
   addr = nir_pack_64_2x32(b, addr);
   addr = nir_iadd(b, addr, nir_imm_int64(b, slot->offset));

   /* Tag writes to gl_PointSize with a special intrinsic */
   if (slot->location == VARYING_SLOT_PSIZ) {
      nir_store_global_psiz_pan(b, data, addr, .access = access);
   } else {
      nir_store_global(b, data, addr, .access = access);
   }
}

static void
build_attr_desc_write(struct nir_builder *b, nir_def *data, uint32_t base,
                      nir_alu_type src_type,
                      const struct lower_vs_outputs_ctx *ctx)
{
   nir_def *index = nir_imm_int(b, base);
   nir_def *vertex_id = nir_load_raw_vertex_id_pan(b);
   nir_def *instance_id = nir_load_instance_id(b);

   nir_def *addr_cvt = nir_lea_attr_pan(b, index, vertex_id, instance_id,
                                        .src_type = src_type);
   nir_def *addr = nir_pack_64_2x32(b, nir_trim_vector(b, addr_cvt, 2));
   nir_def *cvt = nir_channel(b, addr_cvt, 2);

   nir_store_global_cvt_pan(b, data, addr, cvt, .src_type = src_type);
}

static bool
lower_vs_output_store(struct nir_builder *b,
                      nir_intrinsic_instr *store, void *cb_data)
{
   const struct lower_vs_outputs_ctx *ctx = cb_data;

   if (store->intrinsic != nir_intrinsic_store_output &&
       store->intrinsic != nir_intrinsic_store_per_view_output)
      return false;

   b->cursor = nir_instr_remove(&store->instr);

   nir_io_semantics sem = nir_intrinsic_io_semantics(store);
   nir_alu_type src_type = nir_intrinsic_src_type(store);
   unsigned src_bit_size = nir_alu_type_get_type_size(src_type);

   /* Indirect array varyings are not yet supported (num_slots > 1) */
   assert(sem.num_slots == 1);
   assert(nir_src_as_uint(*nir_get_io_offset_src(store)) == 0);

   /* We need the slot section for cache hints */
   pan_varying_layout_require_format(ctx->varying_layout);
   const struct pan_varying_slot *slot =
      pan_varying_layout_find_slot(ctx->varying_layout, sem.location);
   /* Special slots are read only */
   assert(slot && slot->section != PAN_VARYING_SECTION_SPECIAL);
   /* From v9, IO is resized to the real size of the slot */
   assert(ctx->arch < 9 ||
          src_bit_size == nir_alu_type_get_type_size(slot->alu_type));

   /* Since v9 we cannot have separate attribute descriptors for VS-FS,
    * There might be a mismatch on Gallium where the VS thinks it is storing
    * an int, but the data is actually a float, and that's what FS expects.
    * So, just for v9 onwards, just until we haven't fixed gallium, use auto32.
    * We are still getting around the midgard quirk since we do this only
    * from v9.
    * TODO: fix all bugs with gallium and remove this patch
    */
   if (ctx->arch >= 9 && src_bit_size == 32)
      src_type = 32;

   nir_def *data = store->src[0].ssa;
   assert(src_bit_size == data->bit_size);

   /* Trim the input so we don't write extra channels at the end. In effect,
    * we fill in all the intermediate "holes" in the write mask, since we
    * can't mask off stores. Since nir_lower_io_vars_to_temporaries ensures
    * each varying is written at most once, anything that's masked out is
    * undefined, so it doesn't matter what we write there. So we may as well
    * do the simplest thing possible.
    */
   const nir_component_mask_t write_mask = nir_intrinsic_write_mask(store);
   data = nir_trim_vector(b, data, util_last_bit(write_mask));

   if (ctx->arch >= 9 && ctx->has_idvs) {
      uint32_t view_index = 0;
      if (store->intrinsic == nir_intrinsic_store_per_view_output)
         view_index = nir_src_as_uint(store->src[1]);

      build_attr_buf_write(b, data, slot, view_index, ctx);
   } else {
      uint32_t base = nir_intrinsic_base(store);
      assert(store->intrinsic != nir_intrinsic_store_per_view_output);
      build_attr_desc_write(b, data, base, src_type, ctx);
   }

   return true;
}

bool
pan_nir_lower_vs_outputs(nir_shader *shader, unsigned gpu_id,
                         const struct pan_varying_layout *varying_layout,
                         bool has_idvs,
                         bool has_extended_fifo)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);

   const struct lower_vs_outputs_ctx ctx = {
      .arch = pan_arch(gpu_id),
      .varying_layout = varying_layout,
      .has_idvs = has_idvs,
      .has_extended_fifo = has_extended_fifo,
   };
   return nir_shader_intrinsics_pass(shader, lower_vs_output_store,
                                     nir_metadata_control_flow,
                                     (void *)&ctx);
}

struct lower_fs_inputs_ctx {
   unsigned arch;
   const struct pan_varying_layout *varying_layout;
   bool valhall_use_ld_var_buf;
};

static bool
lower_fs_input_load(struct nir_builder *b,
                    nir_intrinsic_instr *load, void *cb_data)
{
   const struct lower_fs_inputs_ctx *ctx = cb_data;

   if (load->intrinsic != nir_intrinsic_load_input &&
       load->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   const nir_io_semantics sem = nir_intrinsic_io_semantics(load);
   const nir_alu_type dest_type = nir_intrinsic_dest_type(load);

   /* Indirect array varyings are not yet supported (num_slots > 1) */
   assert(sem.num_slots == 1);
   assert(nir_src_as_uint(*nir_get_io_offset_src(load)) == 0);

   nir_intrinsic_instr *bary;
   switch (load->intrinsic) {
   case nir_intrinsic_load_input:
      bary = NULL;
      break;
   case nir_intrinsic_load_interpolated_input:
      /* Cannot interpolate ints */
      assert(nir_alu_type_get_base_type(dest_type) == nir_type_float);
      bary = nir_src_as_intrinsic(load->src[0]);
      break;
   default:
      UNREACHABLE("Already handled");
   }

   b->cursor = nir_before_instr(&load->instr);

   const unsigned component = nir_intrinsic_component(load);
   const unsigned load_comps = load->num_components + component;

   nir_def *res;
   if (ctx->valhall_use_ld_var_buf) {
      assert(ctx->arch >= 9);

      pan_varying_layout_require_layout(ctx->varying_layout);
      const struct pan_varying_slot *slot =
         pan_varying_layout_find_slot(ctx->varying_layout,
                                      sem.location);
      assert(slot);
      const nir_alu_type src_type = slot->alu_type;
      nir_def *offset_B = nir_imm_int(b, slot->offset);

      if (load->intrinsic == nir_intrinsic_load_interpolated_input) {
         res = nir_load_var_buf_pan(b, load_comps, load->def.bit_size,
                                    offset_B, &bary->def,
                                    .src_type = src_type,
                                    .io_semantics = sem);
      } else {
         res = nir_load_var_buf_flat_pan(b, load_comps, load->def.bit_size,
                                         offset_B,
                                         .src_type = src_type,
                                         .io_semantics = sem);
      }
   } else {
      const uint32_t base = nir_intrinsic_base(load);
      nir_def *idx = nir_imm_int(b, base);

      if (load->intrinsic == nir_intrinsic_load_interpolated_input) {
         res = nir_load_var_pan(b, load_comps, load->def.bit_size,
                                idx, &bary->def,
                                .dest_type = dest_type,
                                .io_semantics = sem);
      } else {
         res = nir_load_var_flat_pan(b, load_comps, load->def.bit_size, idx,
                                     .dest_type = dest_type,
                                     .io_semantics = sem);
      }
   }

   if (component > 0) {
      unsigned swiz[NIR_MAX_VEC_COMPONENTS] = {0, };
      for (unsigned c = 0; c < load->num_components; c++)
         swiz[c] = component + c;

      res = nir_swizzle(b, res, swiz, load->num_components);
   }

   nir_def_replace(&load->def, res);
   return true;
}

bool
pan_nir_lower_fs_inputs(nir_shader *shader, unsigned gpu_id,
                        const struct pan_varying_layout *varying_layout,
                        bool valhall_use_ld_var_buf)
{
   const struct lower_fs_inputs_ctx ctx = {
      .arch = pan_arch(gpu_id),
      .varying_layout = varying_layout,
      .valhall_use_ld_var_buf = valhall_use_ld_var_buf,
   };
   return nir_shader_intrinsics_pass(shader, lower_fs_input_load,
                                     nir_metadata_control_flow,
                                     (void *)&ctx);
}
