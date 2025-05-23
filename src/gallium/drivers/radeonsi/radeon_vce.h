/**************************************************************************
 *
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#ifndef RADEON_VCE_H
#define RADEON_VCE_H

#include "radeon_video.h"
#include "util/list.h"

#define RVCE_CS(value) (enc->cs.current.buf[enc->cs.current.cdw++] = (value))
#define RVCE_BEGIN(cmd)                                                                            \
   {                                                                                               \
      uint32_t *begin = &enc->cs.current.buf[enc->cs.current.cdw++];                             \
      RVCE_CS(cmd)
#define RVCE_READ(buf, domain, off)                                                                \
   si_vce_add_buffer(enc, (buf), RADEON_USAGE_READ, (domain), (off))
#define RVCE_WRITE(buf, domain, off)                                                               \
   si_vce_add_buffer(enc, (buf), RADEON_USAGE_WRITE, (domain), (off))
#define RVCE_READWRITE(buf, domain, off)                                                           \
   si_vce_add_buffer(enc, (buf), RADEON_USAGE_READWRITE, (domain), (off))
#define RVCE_END()                                                                                 \
   *begin = (&enc->cs.current.buf[enc->cs.current.cdw] - begin) * 4;                             \
   }

#define RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE (4096 * 16 * 2.5)
#define RVCE_MAX_AUX_BUFFER_NUM            4

struct si_screen;

/* driver dependent callback */
typedef void (*rvce_get_buffer)(struct pipe_resource *resource, struct pb_buffer_lean **handle,
                                struct radeon_surf **surface);

struct rvce_rate_control {
   uint32_t rc_method;
   uint32_t target_bitrate;
   uint32_t peak_bitrate;
   uint32_t frame_rate_num;
   uint32_t gop_size;
   uint32_t quant_i_frames;
   uint32_t quant_p_frames;
   uint32_t quant_b_frames;
   uint32_t vbv_buffer_size;
   uint32_t frame_rate_den;
   uint32_t vbv_buf_lv;
   uint32_t max_au_size;
   uint32_t qp_initial_mode;
   uint32_t target_bits_picture;
   uint32_t peak_bits_picture_integer;
   uint32_t peak_bits_picture_fraction;
   uint32_t min_qp;
   uint32_t max_qp;
   uint32_t skip_frame_enable;
   uint32_t fill_data_enable;
   uint32_t enforce_hrd;
   uint32_t b_pics_delta_qp;
   uint32_t ref_b_pics_delta_qp;
   uint32_t rc_reinit_disable;
   uint32_t enc_lcvbr_init_qp_flag;
   uint32_t lcvbrsatd_based_nonlinear_bit_budget_flag;
};

struct rvce_motion_estimation {
   uint32_t enc_ime_decimation_search;
   uint32_t motion_est_half_pixel;
   uint32_t motion_est_quarter_pixel;
   uint32_t disable_favor_pmv_point;
   uint32_t force_zero_point_center;
   uint32_t lsmvert;
   uint32_t enc_search_range_x;
   uint32_t enc_search_range_y;
   uint32_t enc_search1_range_x;
   uint32_t enc_search1_range_y;
   uint32_t disable_16x16_frame1;
   uint32_t disable_satd;
   uint32_t enable_amd;
   uint32_t enc_disable_sub_mode;
   uint32_t enc_ime_skip_x;
   uint32_t enc_ime_skip_y;
   uint32_t enc_en_ime_overw_dis_subm;
   uint32_t enc_ime_overw_dis_subm_no;
   uint32_t enc_ime2_search_range_x;
   uint32_t enc_ime2_search_range_y;
   uint32_t parallel_mode_speedup_enable;
   uint32_t fme0_enc_disable_sub_mode;
   uint32_t fme1_enc_disable_sub_mode;
   uint32_t ime_sw_speedup_enable;
};

struct rvce_pic_control {
   uint32_t enc_use_constrained_intra_pred;
   uint32_t enc_cabac_enable;
   uint32_t enc_cabac_idc;
   uint32_t enc_loop_filter_disable;
   int32_t enc_lf_beta_offset;
   int32_t enc_lf_alpha_c0_offset;
   uint32_t enc_crop_left_offset;
   uint32_t enc_crop_right_offset;
   uint32_t enc_crop_top_offset;
   uint32_t enc_crop_bottom_offset;
   uint32_t enc_num_mbs_per_slice;
   uint32_t enc_intra_refresh_num_mbs_per_slot;
   uint32_t enc_force_intra_refresh;
   uint32_t enc_force_imb_period;
   uint32_t enc_pic_order_cnt_type;
   uint32_t log2_max_pic_order_cnt_lsb_minus4;
   uint32_t enc_sps_id;
   uint32_t enc_pps_id;
   uint32_t enc_constraint_set_flags;
   uint32_t enc_b_pic_pattern;
   uint32_t weight_pred_mode_b_picture;
   uint32_t enc_number_of_reference_frames;
   uint32_t enc_max_num_ref_frames;
   uint32_t enc_num_default_active_ref_l0;
   uint32_t enc_num_default_active_ref_l1;
   uint32_t enc_slice_mode;
   uint32_t enc_max_slice_size;
};

struct rvce_task_info {
   uint32_t offset_of_next_task_info;
   uint32_t task_operation;
   uint32_t reference_picture_dependency;
   uint32_t collocate_flag_dependency;
   uint32_t feedback_index;
   uint32_t video_bitstream_ring_index;
};

struct rvce_feedback_buf_pkg {
   uint32_t feedback_ring_address_hi;
   uint32_t feedback_ring_address_lo;
   uint32_t feedback_ring_size;
};

struct rvce_rdo {
   uint32_t enc_disable_tbe_pred_i_frame;
   uint32_t enc_disable_tbe_pred_p_frame;
   uint32_t use_fme_interpol_y;
   uint32_t use_fme_interpol_uv;
   uint32_t use_fme_intrapol_y;
   uint32_t use_fme_intrapol_uv;
   uint32_t use_fme_interpol_y_1;
   uint32_t use_fme_interpol_uv_1;
   uint32_t use_fme_intrapol_y_1;
   uint32_t use_fme_intrapol_uv_1;
   uint32_t enc_16x16_cost_adj;
   uint32_t enc_skip_cost_adj;
   uint32_t enc_force_16x16_skip;
   uint32_t enc_disable_threshold_calc_a;
   uint32_t enc_luma_coeff_cost;
   uint32_t enc_luma_mb_coeff_cost;
   uint32_t enc_chroma_coeff_cost;
};

struct rvce_enc_operation {
   uint32_t insert_headers;
   uint32_t picture_structure;
   uint32_t allowed_max_bitstream_size;
   uint32_t force_refresh_map;
   uint32_t insert_aud;
   uint32_t end_of_sequence;
   uint32_t end_of_stream;
   uint32_t input_picture_luma_address_hi;
   uint32_t input_picture_luma_address_lo;
   uint32_t input_picture_chroma_address_hi;
   uint32_t input_picture_chroma_address_lo;
   uint32_t enc_input_frame_y_pitch;
   uint32_t enc_input_pic_luma_pitch;
   uint32_t enc_input_pic_chroma_pitch;
   ;
   union {
      struct {
         uint8_t enc_input_pic_addr_mode;
         uint8_t enc_input_pic_swizzle_mode;
         uint8_t enc_disable_two_pipe_mode;
         uint8_t enc_disable_mb_offloading;
      };
      uint32_t enc_input_pic_addr_array_disable2pipe_disablemboffload;
   };
   uint32_t enc_input_pic_tile_config;
   uint32_t enc_pic_type;
   uint32_t enc_idr_flag;
   uint32_t enc_idr_pic_id;
   uint32_t enc_mgs_key_pic;
   uint32_t enc_reference_flag;
   uint32_t enc_temporal_layer_index;
   uint32_t num_ref_idx_active_override_flag;
   uint32_t num_ref_idx_l0_active_minus1;
   uint32_t num_ref_idx_l1_active_minus1;
   uint32_t enc_ref_list_modification_op[4];
   uint32_t enc_ref_list_modification_num[4];
   uint32_t enc_decoded_picture_marking_op[4];
   uint32_t enc_decoded_picture_marking_num[4];
   uint32_t enc_decoded_picture_marking_idx[4];
   uint32_t enc_decoded_ref_base_picture_marking_op[4];
   uint32_t enc_decoded_ref_base_picture_marking_num[4];
   uint32_t l0_dpb_idx;
   uint32_t l0_picture_structure;
   uint32_t l0_enc_pic_type;
   uint32_t l0_frame_number;
   uint32_t l0_picture_order_count;
   uint32_t l0_luma_offset;
   uint32_t l0_chroma_offset;
   uint32_t l1_dpb_idx;
   uint32_t l1_picture_structure;
   uint32_t l1_enc_pic_type;
   uint32_t l1_frame_number;
   uint32_t l1_picture_order_count;
   uint32_t l1_luma_offset;
   uint32_t l1_chroma_offset;
   uint32_t cur_dpb_idx;
   uint32_t enc_reconstructed_luma_offset;
   uint32_t enc_reconstructed_chroma_offset;
   ;
   uint32_t enc_coloc_buffer_offset;
   uint32_t enc_reconstructed_ref_base_picture_luma_offset;
   uint32_t enc_reconstructed_ref_base_picture_chroma_offset;
   uint32_t enc_reference_ref_base_picture_luma_offset;
   uint32_t enc_reference_ref_base_picture_chroma_offset;
   uint32_t picture_count;
   uint32_t frame_number;
   uint32_t picture_order_count;
   uint32_t num_i_pic_remain_in_rcgop;
   uint32_t num_p_pic_remain_in_rcgop;
   uint32_t num_b_pic_remain_in_rcgop;
   uint32_t num_ir_pic_remain_in_rcgop;
   uint32_t enable_intra_refresh;
   uint32_t aq_variance_en;
   uint32_t aq_block_size;
   uint32_t aq_mb_variance_sel;
   uint32_t aq_frame_variance_sel;
   uint32_t aq_param_a;
   uint32_t aq_param_b;
   uint32_t aq_param_c;
   uint32_t aq_param_d;
   uint32_t aq_param_e;
   uint32_t context_in_sfb;
};

struct rvce_enc_create {
   uint32_t enc_use_circular_buffer;
   uint32_t enc_profile;
   uint32_t enc_level;
   uint32_t enc_pic_struct_restriction;
   uint32_t enc_image_width;
   uint32_t enc_image_height;
   uint32_t enc_ref_pic_luma_pitch;
   uint32_t enc_ref_pic_chroma_pitch;
   uint32_t enc_ref_y_height_in_qw;
   uint32_t enc_ref_pic_addr_array_enc_pic_struct_restriction_disable_rdo;
   uint32_t enc_pre_encode_context_buffer_offset;
   uint32_t enc_pre_encode_input_luma_buffer_offset;
   uint32_t enc_pre_encode_input_chroma_buffer_offset;
   union {
      struct {
         uint8_t enc_pre_encode_mode;
         uint8_t enc_pre_encode_chroma_flag;
         uint8_t enc_vbaq_mode;
         uint8_t enc_scene_change_sensitivity;
      };
      uint32_t enc_pre_encode_mode_chromaflag_vbaqmode_scenechangesensitivity;
   };
};

struct rvce_config_ext {
   uint32_t enc_enable_perf_logging;
};

struct rvce_h264_enc_pic {
   struct rvce_rate_control rc;
   struct rvce_motion_estimation me;
   struct rvce_pic_control pc;
   struct rvce_task_info ti;
   struct rvce_feedback_buf_pkg fb;
   struct rvce_rdo rdo;
   struct rvce_enc_operation eo;
   struct rvce_enc_create ec;
   struct rvce_config_ext ce;

   enum pipe_h2645_enc_picture_type picture_type;
   unsigned frame_num;
   unsigned frame_num_cnt;
   unsigned p_remain;
   unsigned i_remain;
   unsigned pic_order_cnt;
   unsigned addrmode_arraymode_disrdo_distwoinstants;

   bool not_referenced;
};

/* VCE encoder representation */
struct rvce_encoder {
   struct pipe_video_codec base;

   unsigned stream_handle;

   struct pipe_screen *screen;
   struct radeon_winsys *ws;
   struct radeon_cmdbuf cs;

   rvce_get_buffer get_buffer;

   struct pb_buffer_lean *handle;
   struct radeon_surf *luma;
   struct radeon_surf *chroma;

   struct pb_buffer_lean *bs_handle;
   unsigned bs_size;
   unsigned bs_offset;

   unsigned dpb_slots;

   struct rvid_buffer *fb;
   struct rvid_buffer dpb;
   struct pipe_h264_enc_picture_desc pic;
   struct rvce_h264_enc_pic enc_pic;

   bool use_vm;
   bool dual_pipe;
   unsigned fw_version;
};

struct rvce_output_unit_segment {
   bool is_slice;
   unsigned size;
   unsigned offset;
};

struct rvce_feedback_data {
   unsigned num_segments;
   struct rvce_output_unit_segment segments[];
};

struct pipe_video_codec *si_vce_create_encoder(struct pipe_context *context,
                                               const struct pipe_video_codec *templat,
                                               struct radeon_winsys *ws,
                                               rvce_get_buffer get_buffer);

bool si_vce_is_fw_version_supported(struct si_screen *sscreen);

void si_vce_add_buffer(struct rvce_encoder *enc, struct pb_buffer_lean *buf, unsigned usage,
                       enum radeon_bo_domain domain, signed offset);

#endif
