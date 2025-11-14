/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <assert.h>
#include <stdint.h>

#include "dev/intel_device_info.h"
#include "util/macros.h"

#include "gen_enums.h"
#include "gen_types.h"
#include "gen_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gen_encode_params {
   const struct intel_device_info *devinfo;

   /* Use compacted encoded form for all instructions that support it. */
   bool compact_all;

   const gen_inst *insts;
   int             num_insts;

   /* Must be non-NULL, used for allocating `errors`. */
   void *mem_ctx;

   /* Must be non-NULL.  Represents a pre-allocated buffer of size
    * raw_bytes_size that will be filled in.
    *
    * The size will be updated to the actual size used.
    */
   void *raw_bytes;
   int   raw_bytes_size;

   /* Optional per-instruction encoded byte offsets.  When non-NULL, it must
    * have at least num_insts entries and will be filled with the start offset
    * of each instruction in the final encoded output.
    */
   int *encoded_offsets;

   gen_error *errors;
   int        num_errors;
} gen_encode_params;

bool gen_encode(gen_encode_params *params);


typedef struct gen_decode_params {
   const struct intel_device_info *devinfo;

   const void *raw_bytes;
   int         raw_bytes_size;

   /* Set true if the decode is only a subset of the program. If a subset is
    * decoded, then it is not possible to adjust jip/uip references properly.
    */
   bool        program_subset;

   /* Must be non-NULL, used for allocating the arrays below. */
   void *mem_ctx;

   gen_inst *insts;
   int       num_insts;

   gen_error *errors;
   int        num_errors;
} gen_decode_params;

bool gen_decode(gen_decode_params *params);


typedef struct gen_inst_layout {
   int offset;
   bool was_compacted;
} gen_inst_layout;

typedef struct gen_scan_raw_layout_params {
   const void *raw_bytes;
   int         raw_bytes_size;

   /* Optional per-instruction output array.  When non-NULL, must have at
    * least num_insts entries.  If the right num_insts is unknown, using an
    * array with size `raw_bytes_size/8` is always sufficient.
    *
    * When the call succeeds, `num_insts` will be updated with the actual
    * number of instructions.
    */
   gen_inst_layout *layouts;
   int              num_insts;

   /* If successful, will contain the byte offset from raw_bytes after the
    * last complete instruction.
    */
   int end_offset;
} gen_scan_raw_layout_params;

bool gen_scan_raw_layout(gen_scan_raw_layout_params *params);


gen_lsc_desc gen_lsc_desc_decode(const struct intel_device_info *devinfo,
                                 uint32_t desc);

uint32_t gen_lsc_desc_encode(const struct intel_device_info *devinfo,
                             const gen_lsc_desc *desc);

gen_lsc_ex_desc gen_lsc_ex_desc_decode(const struct intel_device_info *devinfo,
                                       enum lsc_addr_surface_type addr_type,
                                       uint32_t ex_desc,
                                       uint32_t ex_desc_imm_extra);

uint32_t gen_lsc_ex_desc_encode(const struct intel_device_info *devinfo,
                                const gen_lsc_ex_desc *ex_desc,
                                uint32_t *ex_desc_imm_extra_out);

gen_urb_desc gen_urb_desc_decode(const struct intel_device_info *devinfo,
                                 uint32_t desc);

uint32_t gen_urb_desc_encode(const struct intel_device_info *devinfo,
                             const gen_urb_desc *desc);

gen_sampler_desc gen_sampler_desc_decode(const struct intel_device_info *devinfo,
                                         uint32_t desc);

uint32_t gen_sampler_desc_encode(const struct intel_device_info *devinfo,
                                 const gen_sampler_desc *desc);

gen_hdc_desc gen_hdc_desc_decode(const struct intel_device_info *devinfo,
                                 uint32_t desc);

uint32_t gen_hdc_desc_encode(const struct intel_device_info *devinfo,
                             const gen_hdc_desc *desc);

gen_render_desc gen_render_desc_decode(const struct intel_device_info *devinfo,
                                       uint32_t desc);

uint32_t gen_render_desc_encode(const struct intel_device_info *devinfo,
                                const gen_render_desc *desc);

static inline uint32_t
lsc_msg_desc(const struct intel_device_info *devinfo,
             enum lsc_opcode opcode,
             enum lsc_addr_surface_type addr_type,
             enum lsc_addr_size addr_sz,
             enum lsc_data_size data_sz, unsigned num_channels_or_cmask,
             bool transpose, unsigned cache_ctrl)
{
   assert(devinfo->has_lsc);
   assert(!transpose || lsc_opcode_has_transpose(opcode));

   gen_lsc_desc desc = {};
   desc.op = opcode;
   desc.addr_type = addr_type;
   desc.addr_size = addr_sz;
   desc.data_size = data_sz;
   desc.cache_ctrl = cache_ctrl;

   if (lsc_opcode_has_cmask(opcode)) {
      desc.cmask = (enum lsc_cmask)num_channels_or_cmask;
   } else {
      desc.vect_size = lsc_vect_size(num_channels_or_cmask);
      desc.transpose = transpose;
   }

   return gen_lsc_desc_encode(devinfo, &desc);
}

/* Returns the encoded shader size in bytes starting at `start`.
 *
 * If `end_bound` is non-zero, it is treated as an absolute byte offset from
 * `assembly` and scanning stops before reading past it.  Pass 0 for the old
 * unbounded behavior.
 */
int gen_find_shader_size(const struct intel_device_info *devinfo,
                         const void *assembly,
                         int start,
                         int end_bound);

#ifdef __cplusplus
} /* extern "C" */
#endif
