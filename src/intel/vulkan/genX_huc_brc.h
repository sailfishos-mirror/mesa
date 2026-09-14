/*
 * Copyright (c) 2026 Igalia S.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef GENX_HUC_BRC_H
#define GENX_HUC_BRC_H

#include <math.h>

#include "genX_huc.h"

/* AVC (H.264) VDENC HuC BRC firmware ABI. Unlike HEVC, the AVC BRC DMEM
 * layout is identical on gen12 (ADL-P) and gen12.5 (DG2): DG2 reuses the
 * gen12 encode class, so a single definition serves both. Init DMEM is 192
 * bytes, update DMEM is 448 bytes. Kernel descriptors and BRC flags differ
 * from the HEVC ones.
 */
#define ANV_HUC_AVC_BRC_INIT_KERNEL_DESCRIPTOR    4
#define ANV_HUC_AVC_BRC_UPDATE_KERNEL_DESCRIPTOR  5
#define ANV_HUC_AVC_BRC_FLAG_ICQ                  0x0000
#define ANV_HUC_AVC_BRC_FLAG_CBR                  0x0010
#define ANV_HUC_AVC_BRC_FLAG_VBR                  0x0020
#define ANV_HUC_AVC_BRC_FLAG_VCM                  0x0040
#define ANV_HUC_AVC_BRC_FLAG_LOWDELAY             0x0080

struct anv_huc_avc_brc_init_dmem {
   uint8_t  BRCFunc_U8;
   uint8_t  OpenSourceEnable_U8;
   uint8_t  RVSD[2];
   uint16_t INIT_BRCFlag_U16;
   uint16_t Reserved;
   uint16_t INIT_FrameWidth_U16;
   uint16_t INIT_FrameHeight_U16;
   uint32_t INIT_TargetBitrate_U32;
   uint32_t INIT_MinRate_U32;
   uint32_t INIT_MaxRate_U32;
   uint32_t INIT_BufSize_U32;
   uint32_t INIT_InitBufFull_U32;
   uint32_t INIT_ProfileLevelMaxFrame_U32;
   uint32_t INIT_FrameRateM_U32;
   uint32_t INIT_FrameRateD_U32;
   uint16_t INIT_GopP_U16;
   uint16_t INIT_GopB_U16;
   uint16_t INIT_MinQP_U16;
   uint16_t INIT_MaxQP_U16;
   int8_t   INIT_DevThreshPB0_S8[8];
   int8_t   INIT_DevThreshVBR0_S8[8];
   int8_t   INIT_DevThreshI0_S8[8];
   uint8_t  INIT_InitQPIP;
   uint8_t  INIT_NotUseRhoDm_U8;
   uint8_t  INIT_InitQPB;
   uint8_t  INIT_MbQpCtrl_U8;
   uint8_t  INIT_SliceSizeCtrlEn_U8;
   int8_t   INIT_IntraQPDelta_I8[3];
   int8_t   INIT_SkipQPDelta_I8;
   int8_t   INIT_DistQPDelta_I8[4];
   uint8_t  INIT_OscillationQpDelta_U8;
   uint8_t  INIT_HRDConformanceCheckDisable_U8;
   uint8_t  INIT_SkipFrameEnableFlag;
   uint8_t  INIT_TopQPDeltaThrForAdapt2Pass_U8;
   uint8_t  INIT_TopFrmSzThrForAdapt2Pass_U8;
   uint8_t  INIT_BotFrmSzThrForAdapt2Pass_U8;
   uint8_t  INIT_QPSelectForFirstPass_U8;
   uint8_t  INIT_MBHeaderCompensation_U8;
   uint8_t  INIT_OverShootCarryFlag_U8;
   uint8_t  INIT_OverShootSkipFramePct_U8;
   uint8_t  INIT_EstRateThreshP0_U8[7];
   uint8_t  INIT_EstRateThreshB0_U8[7];
   uint8_t  INIT_EstRateThreshI0_U8[7];
   uint8_t  INIT_FracQPEnable_U8;
   uint8_t  INIT_ScenarioInfo_U8;
   uint8_t  INIT_StaticRegionStreamIn_U8;
   uint8_t  INIT_DeltaQP_Adaptation_U8;
   uint8_t  INIT_MaxCRFQualityFactor_U8;
   uint8_t  INIT_CRFQualityFactor_U8;
   uint8_t  INIT_BotQPDeltaThrForAdapt2Pass_U8;
   uint8_t  INIT_SlidingWindowSize_U8;
   uint8_t  INIT_SlidingWidowRCEnable_U8;
   uint8_t  INIT_SlidingWindowMaxRateRatio_U8;
   uint8_t  INIT_LowDelayGoldenFrameBoost_U8;
   uint8_t  INIT_AdaptiveCostEnable_U8;
   uint8_t  INIT_AdaptiveHMEExtensionEnable_U8;
   uint8_t  INIT_ICQReEncode_U8;
   uint8_t  INIT_LookaheadDepth_U8;
   uint8_t  INIT_SinglePassOnly;
   uint8_t  INIT_New_DeltaQP_Adaptation_U8;
   uint8_t  RSVD2[55];
};

struct anv_huc_avc_brc_update_dmem {
   uint8_t  BRCFunc_U8;
   uint8_t  RSVD[3];
   uint32_t UPD_TARGETSIZE_U32;
   uint32_t UPD_FRAMENUM_U32;
   uint32_t UPD_PeakTxBitsPerFrame_U32;
   uint32_t UPD_FrameBudget_U32;
   uint32_t FrameByteCount;
   uint32_t TimingBudgetOverflow;
   uint32_t ImgStatusCtrl;
   uint32_t IPCMNonConformant;
   uint16_t UPD_startGAdjFrame_U16[4];
   uint16_t UPD_MBBudget_U16[52];
   uint16_t UPD_SLCSZ_TARGETSLCSZ_U16;
   uint16_t UPD_SLCSZ_UPD_THRDELTAI_U16[42];
   uint16_t UPD_SLCSZ_UPD_THRDELTAP_U16[42];
   uint16_t UPD_NumOfFramesSkipped_U16;
   uint16_t UPD_SkipFrameSize_U16;
   uint16_t UPD_StaticRegionPct_U16;
   uint8_t  UPD_gRateRatioThreshold_U8[7];
   uint8_t  UPD_CurrFrameType_U8;
   uint8_t  UPD_startGAdjMult_U8[5];
   uint8_t  UPD_startGAdjDiv_U8[5];
   uint8_t  UPD_gRateRatioThresholdQP_U8[8];
   uint8_t  UPD_PAKPassNum_U8;
   uint8_t  UPD_MaxNumPass_U8;
   uint8_t  UPD_SceneChgWidth_U8[2];
   uint8_t  UPD_SceneChgDetectEn_U8;
   uint8_t  UPD_SceneChgPrevIntraPctThreshold_U8;
   uint8_t  UPD_SceneChgCurIntraPctThreshold_U8;
   uint8_t  UPD_IPAverageCoeff_U8;
   uint8_t  UPD_MinQpAdjustment_U8;
   uint8_t  UPD_TimingBudgetCheck_U8;
   int8_t   reserved_I8[4];
   uint8_t  UPD_CQP_QpValue_U8;
   uint8_t  UPD_CQP_FracQp_U8;
   uint8_t  UPD_HMEDetectionEnable_U8;
   uint8_t  UPD_HMECostEnable_U8;
   uint8_t  UPD_DisablePFrame8x8Transform_U8;
   uint8_t  RSVD3;
   uint8_t  UPD_ROISource_U8;
   uint8_t  RSVD4;
   uint16_t UPD_TargetSliceSize_U16;
   uint16_t UPD_MaxNumSliceAllowed_U16;
   uint16_t UPD_SLBB_Size_U16;
   uint16_t UPD_SLBB_B_Offset_U16;
   uint16_t UPD_AvcImgStateOffset_U16;
   uint16_t reserved_u16;
   uint32_t NumOfSlice;
   uint16_t AveHmeDist_U16;
   uint8_t  HmeDistAvailable_U8;
   uint8_t  DisableDMA;
   uint16_t AdditionalFrameSize_U16;
   uint8_t  AddNALHeaderSizeInternally_U8;
   uint8_t  UPD_RoiQpViaForceQp_U8;
   uint32_t CABACZeroInsertionSize_U32;
   uint32_t MiniFramePaddingSize_U32;
   uint16_t UPD_WidthInMB_U16;
   uint16_t UPD_HeightInMB_U16;
   int8_t   UPD_ROIQpDelta_I8[8];
   int8_t   HME0XOffset_I8;
   int8_t   HME0YOffset_I8;
   int8_t   HME1XOffset_I8;
   int8_t   HME1YOffset_I8;
   uint8_t  MOTION_ADAPTIVE_G4;
   uint8_t  EnableLookAhead;
   uint8_t  UPD_LA_Data_Offset_U8;
   uint8_t  UPD_CQMEnabled_U8;
   uint32_t UPD_LA_TargetSize_U32;
   uint32_t UPD_LA_TargetFulness_U32;
   uint8_t  UPD_Delta_U8;
   uint8_t  UPD_ROM_CURRENT_U8;
   uint8_t  UPD_ROM_ZERO_U8;
   uint8_t  UPD_TCBRC_SCENARIO_U8;
   uint8_t  UPD_EnableFineGrainLA;
   int8_t   UPD_DeltaQpDcOffset;
   uint16_t UPD_NumSlicesForRounding;
   uint32_t UPD_UserMaxFramePB;
   uint8_t  RSVD2[4];
};

static_assert(sizeof(struct anv_huc_avc_brc_init_dmem) == 192, "huc avc brc init dmem layout");
static_assert(offsetof(struct anv_huc_avc_brc_init_dmem, INIT_BRCFlag_U16) == 4, "huc avc brc init dmem layout");
static_assert(offsetof(struct anv_huc_avc_brc_init_dmem, INIT_DevThreshPB0_S8) == 52, "huc avc brc init dmem layout");
static_assert(offsetof(struct anv_huc_avc_brc_init_dmem, INIT_InitQPIP) == 76, "huc avc brc init dmem layout");
static_assert(offsetof(struct anv_huc_avc_brc_init_dmem, INIT_EstRateThreshP0_U8) == 99, "huc avc brc init dmem layout");
static_assert(offsetof(struct anv_huc_avc_brc_init_dmem, INIT_New_DeltaQP_Adaptation_U8) == 136, "huc avc brc init dmem layout");

static_assert(sizeof(struct anv_huc_avc_brc_update_dmem) == 448, "huc avc brc update dmem layout");
static_assert(offsetof(struct anv_huc_avc_brc_update_dmem, UPD_TARGETSIZE_U32) == 4, "huc avc brc update dmem layout");
static_assert(offsetof(struct anv_huc_avc_brc_update_dmem, UPD_MBBudget_U16) == 44, "huc avc brc update dmem layout");
static_assert(offsetof(struct anv_huc_avc_brc_update_dmem, NumOfSlice) == 384, "huc avc brc update dmem layout");
static_assert(offsetof(struct anv_huc_avc_brc_update_dmem, UPD_UserMaxFramePB) == 440, "huc avc brc update dmem layout");


/* H.264 AVC BRC region-5 constant-data buffer (1728 bytes) and the source
 * tables the driver copies into it, extracted verbatim from the media-driver
 * (CBR/VBR scope). Field names follow the media layout for cross-reference.
 */
struct anv_huc_avc_brc_const_data {
   uint8_t UPD_GlobalRateQPAdjTabI_U8[64];
   uint8_t UPD_GlobalRateQPAdjTabP_U8[64];
   uint8_t UPD_GlobalRateQPAdjTabB_U8[64];
   uint8_t UPD_DistThreshldI_U8[10];
   uint8_t UPD_DistThreshldP_U8[10];
   uint8_t UPD_DistThreshldB_U8[10];
   uint8_t UPD_DistQPAdjTabI_U8[81];
   uint8_t UPD_DistQPAdjTabP_U8[81];
   uint8_t UPD_DistQPAdjTabB_U8[81];
   int8_t  UPD_BufRateAdjTabI_S8[72];
   int8_t  UPD_BufRateAdjTabP_S8[72];
   int8_t  UPD_BufRateAdjTabB_S8[72];
   uint8_t UPD_FrmSzMinTabP_U8[9];
   uint8_t UPD_FrmSzMinTabB_U8[9];
   uint8_t UPD_FrmSzMinTabI_U8[9];
   uint8_t UPD_FrmSzMaxTabP_U8[9];
   uint8_t UPD_FrmSzMaxTabB_U8[9];
   uint8_t UPD_FrmSzMaxTabI_U8[9];
   uint8_t UPD_FrmSzSCGTabP_U8[9];
   uint8_t UPD_FrmSzSCGTabB_U8[9];
   uint8_t UPD_FrmSzSCGTabI_U8[9];
   uint8_t UPD_I_IntraNonPred[42];
   uint8_t UPD_I_Intra16x16[42];
   uint8_t UPD_I_Intra8x8[42];
   uint8_t UPD_I_Intra4x4[42];
   uint8_t UPD_I_IntraChroma[42];
   uint8_t UPD_P_IntraNonPred[42];
   uint8_t UPD_P_Intra16x16[42];
   uint8_t UPD_P_Intra8x8[42];
   uint8_t UPD_P_Intra4x4[42];
   uint8_t UPD_P_IntraChroma[42];
   uint8_t UPD_P_Inter16x8[42];
   uint8_t UPD_P_Inter8x8[42];
   uint8_t UPD_P_Inter16x16[42];
   uint8_t UPD_P_RefId[42];
#if GFX_VERx10 >= 125
   uint8_t Reserved[630];
#else
   uint8_t UPD_HMEMVCost[8][42];
   uint8_t RSVD[42];
#endif
};

#if GFX_VERx10 >= 125
static_assert(sizeof(struct anv_huc_avc_brc_const_data) == 1980, "huc avc brc const data layout");
#else
static_assert(sizeof(struct anv_huc_avc_brc_const_data) == 1728, "huc avc brc const data layout");
#endif

/* GlobalRateQPAdjTab (uint8/int8 storage, [64]) */
static const int8_t anv_h264_brc_global_rate_qp_adj_i[64] =
{
   48, 40, 32, 24, 16, 8, 0, -8, 40, 32, 24, 16, 8, 0, -8, -16,
   32, 24, 16, 8, 0, -8, -16, -24, 24, 16, 8, 0, -8, -16, -24, -32,
   16, 8, 0, -8, -16, -24, -32, -40, 8, 0, -8, -16, -24, -32, -40, -48,
   0, -8, -16, -24, -32, -40, -48, -56, 48, 40, 32, 24, 16, 8, 0, -8
};

static const int8_t anv_h264_brc_global_rate_qp_adj_p[64] =
{
   48, 40, 32, 24, 16, 8, 0, -8, 40, 32, 24, 16, 8, 0, -8, -16,
   16, 8, 8, 4, -8, -16, -16, -24, 8, 0, 0, -8, -16, -16, -16, -24,
   8, 0, 0, -24, -32, -32, -32, -48, 0, -16, -16, -24, -32, -48, -56, -64,
   -8, -16, -32, -32, -48, -48, -56, -64, -16, -32, -48, -48, -48, -56, -64, -80
};

static const int8_t anv_h264_brc_global_rate_qp_adj_b[64] =
{
   48, 40, 32, 24, 16, 8, 0, -8, 40, 32, 24, 16, 8, 0, -8, -16,
   32, 24, 16, 8, 0, -8, -16, -24, 24, 16, 8, 0, -8, -8, -16, -24,
   16, 8, 0, 0, -8, -16, -24, -32, 16, 8, 0, 0, -8, -16, -24, -32,
   0, -8, -8, -16, -32, -48, -56, -64, 0, -8, -8, -16, -32, -48, -56, -64
};


/* DistThreshld ([10]); B reuses P at fill time */
static const uint8_t anv_h264_brc_dist_threshld_i[10] =
{
   2, 4, 8, 12, 19, 32, 64, 128, 0, 0
};

static const uint8_t anv_h264_brc_dist_threshld_p[10] =
{
   2, 4, 8, 12, 19, 32, 64, 128, 0, 0
};


/* DistQPAdjTab ([81]) CBR */
static const int8_t anv_h264_brc_cbr_dist_qp_adj_i[81] =
{
   0, 0, 0, 0, 0, 3, 4, 6, 8, 0, 0, 0, 0, 0, 2, 3,
   5, 7, -1, 0, 0, 0, 0, 2, 2, 4, 5, -1, -1, 0, 0, 0,
   1, 2, 2, 4, -2, -2, -1, 0, 0, 0, 1, 2, 4, -2, -2, -1,
   0, 0, 0, 1, 2, 4, -3, -2, -1, -1, 0, 0, 1, 2, 5, -3,
   -2, -1, -1, 0, 0, 2, 4, 7, -4, -3, -2, -1, 0, 1, 3, 5,
   8
};

static const int8_t anv_h264_brc_cbr_dist_qp_adj_p[81] =
{
   -1, 0, 0, 0, 0, 1, 1, 2, 3, -1, -1, 0, 0, 0, 1, 1,
   2, 3, -2, -1, -1, 0, 0, 1, 1, 2, 3, -3, -2, -2, -1, 0,
   0, 1, 2, 3, -3, -2, -1, -1, 0, 0, 1, 2, 3, -3, -2, -1,
   -1, 0, 0, 1, 2, 3, -3, -2, -1, -1, 0, 0, 1, 2, 3, -3,
   -2, -1, -1, 0, 0, 1, 2, 3, -3, -2, -1, -1, 0, 0, 1, 2,
   3
};

static const int8_t anv_h264_brc_cbr_dist_qp_adj_b[81] =
{
   0, 0, 0, 0, 0, 2, 3, 3, 4, 0, 0, 0, 0, 0, 2, 3,
   3, 4, -1, 0, 0, 0, 0, 2, 2, 3, 3, -1, -1, 0, 0, 0,
   1, 2, 2, 2, -1, -1, -1, 0, 0, 0, 1, 2, 2, -2, -1, -1,
   0, 0, 0, 0, 1, 2, -2, -1, -1, -1, 0, 0, 0, 1, 3, -2,
   -2, -1, -1, 0, 0, 1, 1, 3, -2, -2, -1, -1, 0, 1, 1, 2,
   4
};


/* DistQPAdjTab ([81]) VBR */
static const int8_t anv_h264_brc_vbr_dist_qp_adj_i[81] =
{
   0, 0, 0, 0, 0, 3, 4, 6, 8, 0, 0, 0, 0, 0, 2, 3,
   5, 7, -1, 0, 0, 0, 0, 2, 2, 4, 5, -1, -1, 0, 0, 0,
   1, 2, 2, 4, -2, -2, -1, 0, 0, 0, 1, 2, 4, -2, -2, -1,
   0, 0, 0, 1, 2, 4, -3, -2, -1, -1, 0, 0, 1, 2, 5, -3,
   -2, -1, -1, 0, 0, 2, 4, 7, -4, -3, -2, -1, 0, 1, 3, 5,
   8
};

static const int8_t anv_h264_brc_vbr_dist_qp_adj_p[81] =
{
   -1, 0, 0, 0, 0, 1, 1, 2, 3, -1, -1, 0, 0, 0, 1, 1,
   2, 3, -2, -1, -1, 0, 0, 1, 1, 2, 3, -3, -2, -2, -1, 0,
   0, 1, 2, 3, -3, -2, -1, -1, 0, 0, 1, 2, 3, -3, -2, -1,
   -1, 0, 0, 1, 2, 3, -3, -2, -1, -1, 0, 0, 1, 2, 3, -3,
   -2, -1, -1, 0, 0, 1, 2, 3, -3, -2, -1, -1, 0, 0, 1, 2,
   3
};

static const int8_t anv_h264_brc_vbr_dist_qp_adj_b[81] =
{
   0, 0, 0, 0, 0, 2, 3, 3, 4, 0, 0, 0, 0, 0, 2, 3,
   3, 4, -1, 0, 0, 0, 0, 2, 2, 3, 3, -1, -1, 0, 0, 0,
   1, 2, 2, 2, -1, -1, -1, 0, 0, 0, 1, 2, 2, -2, -1, -1,
   0, 0, 0, 0, 1, 2, -2, -1, -1, -1, 0, 0, 0, 1, 3, -2,
   -2, -1, -1, 0, 0, 1, 1, 3, -2, -2, -1, -1, 0, 1, 1, 2,
   4
};


/* FrmSzAdjTab / BufRateAdjTab ([72], int8) CBR */
static const int8_t anv_h264_brc_cbr_frm_sz_adj_i[72] =
{
   -4, -20, -28, -36, -40, -44, -48, -80, 0, -8, -12, -20, -24, -28, -32, -36,
   0, 0, -8, -16, -20, -24, -28, -32, 8, 4, 0, 0, -8, -16, -24, -28,
   32, 24, 16, 2, -4, -8, -16, -20, 36, 32, 28, 16, 8, 0, -4, -8,
   40, 36, 24, 20, 16, 8, 0, -8, 48, 40, 28, 24, 20, 12, 0, -4,
   64, 48, 28, 20, 16, 12, 8, 4
};

static const int8_t anv_h264_brc_cbr_frm_sz_adj_p[72] =
{
   -8, -24, -32, -44, -48, -56, -64, -80, -8, -16, -32, -40, -44, -52, -56, -64,
   0, 0, -16, -28, -36, -40, -44, -48, 8, 4, 0, 0, -8, -16, -24, -36,
   20, 12, 4, 0, -8, -8, -8, -16, 24, 16, 8, 8, 8, 0, -4, -8,
   40, 36, 24, 20, 16, 8, 0, -8, 48, 40, 28, 24, 20, 12, 0, -4,
   64, 48, 28, 20, 16, 12, 8, 4
};

static const int8_t anv_h264_brc_cbr_frm_sz_adj_b[72] =
{
   0, -4, -8, -16, -24, -32, -40, -48, 1, 0, -4, -8, -16, -24, -32, -40,
   4, 2, 0, -1, -3, -8, -16, -24, 8, 4, 2, 0, -1, -4, -8, -16,
   20, 16, 4, 0, -1, -4, -8, -16, 24, 20, 16, 8, 4, 0, -4, -8,
   28, 24, 20, 16, 8, 4, 0, -8, 32, 24, 20, 16, 8, 4, 0, -4,
   64, 48, 28, 20, 16, 12, 8, 4
};


/* FrmSzAdjTab / BufRateAdjTab ([72], int8) VBR */
static const int8_t anv_h264_brc_vbr_frm_sz_adj_i[72] =
{
   -4, -20, -28, -36, -40, -44, -48, -80, 0, -8, -12, -20, -24, -28, -32, -36,
   0, 0, -8, -16, -20, -24, -28, -32, 8, 4, 0, 0, -8, -16, -24, -28,
   32, 24, 16, 2, -4, -8, -16, -20, 36, 32, 28, 16, 8, 0, -4, -8,
   40, 36, 24, 20, 16, 8, 0, -8, 48, 40, 28, 24, 20, 12, 0, -4,
   64, 48, 28, 20, 16, 12, 8, 4
};

static const int8_t anv_h264_brc_vbr_frm_sz_adj_p[72] =
{
   -8, -24, -32, -44, -48, -56, -64, -80, -8, -16, -32, -40, -44, -52, -56, -64,
   0, 0, -16, -28, -36, -40, -44, -48, 8, 4, 0, 0, -8, -16, -24, -36,
   20, 12, 4, 0, -8, -8, -8, -16, 24, 16, 8, 8, 8, 0, -4, -8,
   40, 36, 24, 20, 16, 8, 0, -8, 48, 40, 28, 24, 20, 12, 0, -4,
   64, 48, 28, 20, 16, 12, 8, 4
};

static const int8_t anv_h264_brc_vbr_frm_sz_adj_b[72] =
{
   0, -4, -8, -16, -24, -32, -40, -48, 1, 0, -4, -8, -16, -24, -32, -40,
   4, 2, 0, -1, -3, -8, -16, -24, 8, 4, 2, 0, -1, -4, -8, -16,
   20, 16, 4, 0, -1, -4, -8, -16, 24, 20, 16, 8, 4, 0, -4, -8,
   28, 24, 20, 16, 8, 4, 0, -8, 32, 24, 20, 16, 8, 4, 0, -4,
   64, 48, 28, 20, 16, 12, 8, 4
};


/* FrmSz Min/Max/SCG ([9]); I and P only */
static const uint8_t anv_h264_brc_frm_sz_min_i[9] =
{
   1, 2, 4, 8, 16, 20, 24, 32, 36
};

static const uint8_t anv_h264_brc_frm_sz_min_p[9] =
{
   1, 2, 4, 6, 8, 10, 16, 16, 16
};

static const uint8_t anv_h264_brc_frm_sz_max_i[9] =
{
   48, 64, 80, 96, 112, 128, 144, 160, 160
};

static const uint8_t anv_h264_brc_frm_sz_max_p[9] =
{
   48, 64, 80, 96, 112, 128, 144, 160, 160
};

static const uint8_t anv_h264_brc_frm_sz_scg_i[9] =
{
   4, 8, 12, 16, 20, 24, 24, 0, 0
};

static const uint8_t anv_h264_brc_frm_sz_scg_p[9] =
{
   4, 8, 12, 16, 20, 24, 24, 0, 0
};


/* Cost tables ([42]) copied verbatim by legacy FillHucConstData */
static const uint8_t anv_h264_brc_cost_i_intra_non_pred[42] =
{
   14, 14, 14, 24, 25, 27, 28, 13, 15, 24, 25, 13, 15, 15, 12, 14,
   12, 12, 10, 10, 11, 10, 10, 10, 9, 9, 8, 8, 8, 8, 8, 8,
   8, 8, 8, 8, 8, 7, 7, 7, 7, 7
};

static const uint8_t anv_h264_brc_cost_i_intra8x8[42] =
{
   0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 4, 4, 4, 4, 6, 6, 6, 6, 6, 6, 6,
   6, 6, 6, 6, 6, 7, 7, 7, 7, 7
};

static const uint8_t anv_h264_brc_cost_i_intra4x4[42] =
{
   46, 46, 46, 56, 57, 58, 59, 44, 46, 56, 57, 45, 47, 56, 46, 56,
   46, 56, 47, 46, 56, 56, 56, 56, 47, 47, 47, 46, 45, 44, 43, 42,
   41, 40, 30, 28, 27, 26, 25, 24, 14, 13
};

static const uint8_t anv_h264_brc_cost_p_intra_non_pred[42] =
{
   6, 6, 6, 7, 8, 9, 10, 5, 6, 7, 8, 6, 7, 7, 7, 7,
   6, 7, 7, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7
};

static const uint8_t anv_h264_brc_cost_p_intra16x16[42] =
{
   27, 27, 27, 28, 30, 40, 41, 26, 27, 28, 30, 26, 28, 29, 27, 28,
   28, 28, 28, 27, 28, 28, 29, 28, 28, 28, 28, 28, 28, 28, 28, 28,
   28, 28, 28, 28, 28, 28, 28, 28, 28, 28
};

static const uint8_t anv_h264_brc_cost_p_intra8x8[42] =
{
   29, 29, 29, 30, 40, 41, 42, 27, 29, 30, 40, 28, 29, 31, 29, 30,
   29, 30, 29, 29, 31, 30, 30, 30, 29, 30, 30, 29, 30, 30, 30, 30,
   30, 30, 30, 30, 30, 30, 30, 30, 30, 30
};

static const uint8_t anv_h264_brc_cost_p_intra4x4[42] =
{
   56, 56, 56, 57, 58, 59, 61, 46, 56, 57, 58, 47, 57, 58, 56, 57,
   56, 57, 57, 56, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
   57, 57, 57, 57, 57, 57, 57, 57, 57, 57
};

static const uint8_t anv_h264_brc_cost_p_inter16x8[42] =
{
   7, 7, 7, 8, 9, 11, 12, 6, 7, 9, 10, 7, 8, 9, 8, 9,
   8, 9, 8, 8, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8,
   9, 9, 9, 9, 9, 9, 9, 9, 9, 9
};

static const uint8_t anv_h264_brc_cost_p_inter8x8[42] =
{
   2, 2, 2, 2, 3, 3, 3, 2, 2, 2, 3, 2, 2, 2, 2, 3,
   2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
   3, 3, 3, 3, 3, 3, 3, 3, 3, 3
};

static const uint8_t anv_h264_brc_cost_p_inter16x16[42] =
{
   5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
   6, 6, 6, 6, 6, 6, 6, 6, 6, 6
};

static const uint8_t anv_h264_brc_cost_p_ref_id[42] =
{
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4
};


/* HME/MV cost SOURCE tables (compute UPD_HMEMVCost[8][42] at runtime) */
static const uint32_t anv_h264_brc_hme_cost[8][52] =
{
   {
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
   },
   {
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
   },
   {
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
   },
   {
   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
   },
   {
   10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
   10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
   10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
   10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10
   },
   {
   10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
   10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
   10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
   10, 10, 10, 10, 20, 30, 40, 50, 50, 50, 50, 50, 50
   },
   {
   20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
   20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
   20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
   20, 20, 20, 40, 60, 80, 100, 100, 100, 100, 100, 100, 100
   },
   {
   20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
   20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
   20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
   20, 20, 30, 50, 100, 200, 200, 200, 200, 200, 200, 200, 200
   }
};

#if GFX_VERx10 >= 125
static const uint8_t h264_vdenc_brc_huc_const_reserved[5][630] = {
   {
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  31,  31,
       31,  31,  31,  31,  31,  31,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  31,  42,  45,  45,  45,  45,  45,  45,  45,  45,  28,  28,  28,  32,  36,  44,
       48,  26,  30,  32,  36,  26,  30,  30,  24,  28,  24,  24,  20,  20,  22,  20,  20,  20,
       18,  18,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  14,  14,  14,  14,  14,
        2,   0,   2,   0,   2,   0,   3,   0,   4,   0,   5,   0,   6,   0,   8,   0,  10,   0,
       13,   0,  16,   0,  20,   0,  26,   0,  33,   0,  41,   0,  52,   0,  66,   0,  83,   0,
      104,   0, 132,   0, 166,   0, 209,   0,   8,   1,  76,   1, 163,   1,  16,   2, 153,   2,
       70,   3,  32,   4,  51,   5, 141,   6,  65,   8, 102,  10,  26,  13, 130,  16, 204,  20,
       52,  26,   4,  33, 153,  41, 105,  52,   9,  66,  51,  83,   2,   0,   2,   0,   2,   0,
        2,   0,   3,   0,   3,   0,   4,   0,   4,   0,   5,   0,   5,   0,   6,   0,   7,   0,
        8,   0,   9,   0,  10,   0,  11,   0,  13,   0,  14,   0,  16,   0,  18,   0,  20,   0,
       23,   0,  26,   0,  29,   0,  33,   0,  37,   0,  41,   0,  46,   0,  52,   0,  58,   0,
       66,   0,  74,   0,  83,   0,  93,   0, 104,   0, 117,   0, 132,   0, 148,   0, 166,   0,
      186,   0, 209,   0, 235,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
   },
   {
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  31,  31,
       31,  31,  31,  31,  31,  31,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  31,  42,  45,  45,  45,  45,  45,  45,  45,  45,  12,  12,  12,  14,  16,  18,
       20,  10,  12,  14,  16,  12,  14,  14,  14,  14,  12,  14,  14,  12,  14,  14,  14,  14,
       14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,
        2,   0,   2,   0,   2,   0,   2,   0,   3,   0,   4,   0,   5,   0,   6,   0,   8,   0,
       10,   0,  12,   0,  16,   0,  20,   0,  25,   0,  32,   0,  40,   0,  50,   0,  64,   0,
       80,   0, 101,   0, 128,   0, 161,   0, 203,   0,   0,   1,  66,   1, 150,   1,   0,   2,
      133,   2,  44,   3,   0,   4,  10,   5,  89,   6,   0,   8,  20,  10, 178,  12,   0,  16,
       40,  20, 101,  25,   0,  32,  81,  40, 203,  50,   0,  64,   3,   0,   3,   0,   3,   0,
        3,   0,   3,   0,   4,   0,   4,   0,   5,   0,   6,   0,   6,   0,   7,   0,   8,   0,
        9,   0,  10,   0,  12,   0,  13,   0,  15,   0,  16,   0,  19,   0,  21,   0,  24,   0,
       26,   0,  30,   0,  33,   0,  38,   0,  42,   0,  48,   0,  53,   0,  60,   0,  67,   0,
       76,   0,  85,   0,  96,   0, 107,   0, 120,   0, 135,   0, 152,   0, 171,   0, 192,   0,
      215,   0, 241,   0,  15,   1,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,
        4,   4,   4,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
        3,   3,   3,   3,   3,   3,   2,   2,   2,   2,   2,   2,   4,   4,   4,   4,   4,   4,
        4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   3,
        3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3
   },
   {
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  31,  31,
       31,  31,  31,  31,  31,  31,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  31,  42,  45,  45,  45,  45,  45,  45,  45,  45,  12,  12,  12,  14,  16,  18,
       20,  10,  12,  14,  16,  12,  14,  14,  14,  14,  12,  14,  14,  12,  14,  14,  14,  14,
       14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,
        3,   0,   3,   0,   3,   0,   4,   0,   5,   0,   6,   0,   8,   0,  10,   0,  12,   0,
       16,   0,  20,   0,  25,   0,  32,   0,  40,   0,  51,   0,  64,   0,  81,   0, 102,   0,
      129,   0, 162,   0, 204,   0,   2,   1,  69,   1, 153,   1,   4,   2, 138,   2,  51,   3,
        8,   4,  20,   5, 102,   6,  16,   8,  40,  10, 204,  12,  32,  16,  81,  20, 153,  25,
       65,  32, 163,  40,  51,  51, 130,  64,  70,  81, 102, 102,   3,   0,   3,   0,   3,   0,
        4,   0,   4,   0,   5,   0,   5,   0,   6,   0,   7,   0,   8,   0,   9,   0,  10,   0,
       11,   0,  12,   0,  14,   0,  16,   0,  18,   0,  20,   0,  22,   0,  25,   0,  28,   0,
       32,   0,  36,   0,  40,   0,  45,   0,  51,   0,  57,   0,  64,   0,  72,   0,  81,   0,
       91,   0, 102,   0, 115,   0, 129,   0, 145,   0, 162,   0, 182,   0, 205,   0, 230,   0,
        2,   1,  34,   1,  69,   1,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
        3,   3,   3,   3,   3,   3,   3,   3,   3,   2,   2,   2,   2,   2,   2,   2,   2,   1,
        1,   1,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   4,   4,   4,   4,   4,   4,
        4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,
        4,   3,   3,   3,   3,   2,   2,   2,   2,   1,   1,   1,   1,   0,   0,   0,   0,   0
   },
   {
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  31,  31,
       31,  31,  31,  31,  31,  31,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  31,  42,  45,  45,  45,  45,  45,  45,  45,  45,  14,  14,  14,  14,  14,  14,
       14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,
       14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,
        4,   0,   4,   0,   4,   0,   6,   0,   7,   0,   9,   0,  12,   0,  15,   0,  19,   0,
       24,   0,  30,   0,  38,   0,  48,   0,  60,   0,  76,   0,  96,   0, 121,   0, 153,   0,
      193,   0, 243,   0,  51,   1, 131,   1, 231,   1, 102,   2,   6,   3, 207,   3, 204,   4,
       12,   6, 158,   7, 153,   9,  24,  12,  61,  15,  51,  19,  48,  24, 122,  30, 102,  38,
       97,  48, 244,  60, 204,  76, 195,  96, 233, 121, 153, 153,   4,   0,   4,   0,   4,   0,
        4,   0,   5,   0,   6,   0,   6,   0,   7,   0,   8,   0,   9,   0,  11,   0,  12,   0,
       13,   0,  15,   0,  17,   0,  19,   0,  22,   0,  24,   0,  27,   0,  31,   0,  35,   0,
       39,   0,  44,   0,  49,   0,  55,   0,  62,   0,  70,   0,  79,   0,  88,   0,  99,   0,
      111,   0, 125,   0, 140,   0, 158,   0, 177,   0, 199,   0, 223,   0, 250,   0,  25,   1,
       60,   1,  98,   1, 142,   1,   3,   3,   3,   3,   3,   3,   3,   3,   3,   2,   2,   2,
        2,   1,   1,   1,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   4,   4,   4,   4,   4,   4,
        4,   4,   4,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
        3,   3,   3,   3,   3,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2
   },
   {
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
        5,   5,   5,   5,   5,   5,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  31,  31,
       31,  31,  31,  31,  31,  31,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
       26,  26,  31,  42,  45,  45,  45,  45,  45,  45,  45,  45,  14,  14,  14,  14,  14,  14,
       14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,
       14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,
        3,   0,   3,   0,   3,   0,   4,   0,   5,   0,   7,   0,   9,   0,  11,   0,  14,   0,
       18,   0,  22,   0,  28,   0,  36,   0,  45,   0,  57,   0,  72,   0,  91,   0, 115,   0,
      145,   0, 182,   0, 230,   0,  34,   1, 109,   1, 204,   1,  68,   2, 219,   2, 153,   3,
      137,   4, 182,   5,  51,   7,  18,   9, 109,  11, 102,  14,  36,  18, 219,  22, 204,  28,
       73,  36, 183,  45, 153,  57, 146,  72, 111,  91,  51, 115,   3,   0,   3,   0,   3,   0,
        4,   0,   4,   0,   5,   0,   5,   0,   6,   0,   7,   0,   8,   0,   9,   0,  10,   0,
       11,   0,  12,   0,  14,   0,  16,   0,  18,   0,  20,   0,  22,   0,  25,   0,  28,   0,
       32,   0,  36,   0,  40,   0,  45,   0,  51,   0,  57,   0,  64,   0,  72,   0,  81,   0,
       91,   0, 102,   0, 115,   0, 129,   0, 145,   0, 162,   0, 182,   0, 205,   0, 230,   0,
        2,   1,  34,   1,  69,   1,   3,   3,   3,   3,   3,   3,   3,   3,   2,   2,   2,   2,
        2,   2,   2,   2,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
        1,   1,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,   4,   4,   4,   4,   4,   4,
        4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,
        3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3
   }
};
#endif

#endif /* GENX_HUC_BRC_H */
