/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_video_dec.h"

uint32_t
ac_video_dec_dpb_size(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   return 0;
}

uint32_t
ac_video_dec_dpb_alignment(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   return 0;
}

struct ac_video_dec *
ac_create_video_decoder(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   return NULL;
}
