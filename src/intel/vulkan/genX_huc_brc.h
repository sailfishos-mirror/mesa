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

static int32_t
anv_brc_estimate_init_qp(double frame_size, double frame_rate, double bitrate)
{
   const double x0 = 0.0, y0 = 1.19, x1 = 1.75, y1 = 1.75;
   double x = log10(frame_size * 2.0 / 3.0 * frame_rate / bitrate);

   return (int32_t)(1.0 / 1.2 *
                    pow(10.0, (x - x0) * (y1 - y0) / (x1 - x0) + y0) + 0.5) + 2;
}

#if GFX_VER >= 12
/* H.264 (AVC) HuC BRC building blocks. gen12+ only. Mirrors the HEVC BRC
 * helpers but targets the MFX PAK second level batch buffer and the AVC DMEM.
 */
/* Second level batch buffer (SLBB) image-state group size. gen120 is
 * MFX_AVC_IMG_STATE (84) + VDENC_IMG_STATE (140) + MI_BATCH_BUFFER_END (4) =
 * 228 bytes. gen125 replaces VDENC_IMG_STATE with VDENC_CMD3 +
 * VDENC_AVC_IMG_STATE. The HuC-patched SLBB carries only this image-state
 * group on both generations; the PAK slices stay in the primary batch.
 */
#if GFX_VERx10 < 125
#define ANV_H264_BRC_SLB_SIZE \
   ((GENX(MFX_AVC_IMG_STATE_length) + GENX(VDENC_IMG_STATE_length) + \
     GENX(MI_BATCH_BUFFER_END_length)) * 4)
#else
#define ANV_H264_BRC_SLB_SIZE \
   ((GENX(MFX_AVC_IMG_STATE_length) + GENX(VDENC_CMD3_length) + \
     GENX(VDENC_AVC_IMG_STATE_length) + GENX(MI_BATCH_BUFFER_END_length)) * 4)
#endif

/* 4-4 (exp/mantissa) LUT compression, from media-driver */
static uint8_t
anv_h264_brc_map44_lut(uint32_t v, uint8_t max)
{
   if (v == 0)
      return 0;
   uint32_t max_cost = (uint32_t)(max & 0xf) << (max >> 4);
   if (v >= max_cost)
      return max;
   int d = (int)(31 - __builtin_clz(v)) - 3;
   if (d < 0)
      d = 0;
   uint32_t ret = ((uint32_t)d << 4) +
                  ((v + (d ? (1u << (d - 1)) : 0)) >> d);
   if ((ret & 0xf) == 0)
      ret |= 8;
   return (uint8_t)ret;
}

static void
anv_h264_brc_fill_const_data(struct anv_video_session *vid,
                             StdVideoH264PictureType pic_type,
                             struct anv_huc_avc_brc_const_data *cd)
{
   bool is_cbr = vid->rc_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR;

   memcpy(cd->UPD_GlobalRateQPAdjTabI_U8, anv_h264_brc_global_rate_qp_adj_i,
          sizeof(cd->UPD_GlobalRateQPAdjTabI_U8));
   memcpy(cd->UPD_GlobalRateQPAdjTabP_U8, anv_h264_brc_global_rate_qp_adj_p,
          sizeof(cd->UPD_GlobalRateQPAdjTabP_U8));
   memcpy(cd->UPD_GlobalRateQPAdjTabB_U8, anv_h264_brc_global_rate_qp_adj_b,
          sizeof(cd->UPD_GlobalRateQPAdjTabB_U8));

   memcpy(cd->UPD_DistThreshldI_U8, anv_h264_brc_dist_threshld_i,
          sizeof(cd->UPD_DistThreshldI_U8));
   memcpy(cd->UPD_DistThreshldP_U8, anv_h264_brc_dist_threshld_p,
          sizeof(cd->UPD_DistThreshldP_U8));
   memcpy(cd->UPD_DistThreshldB_U8, anv_h264_brc_dist_threshld_p,
          sizeof(cd->UPD_DistThreshldB_U8));

   if (is_cbr) {
      memcpy(cd->UPD_DistQPAdjTabI_U8, anv_h264_brc_cbr_dist_qp_adj_i,
             sizeof(cd->UPD_DistQPAdjTabI_U8));
      memcpy(cd->UPD_DistQPAdjTabP_U8, anv_h264_brc_cbr_dist_qp_adj_p,
             sizeof(cd->UPD_DistQPAdjTabP_U8));
      memcpy(cd->UPD_DistQPAdjTabB_U8, anv_h264_brc_cbr_dist_qp_adj_b,
             sizeof(cd->UPD_DistQPAdjTabB_U8));
      memcpy(cd->UPD_BufRateAdjTabI_S8, anv_h264_brc_cbr_frm_sz_adj_i,
             sizeof(cd->UPD_BufRateAdjTabI_S8));
      memcpy(cd->UPD_BufRateAdjTabP_S8, anv_h264_brc_cbr_frm_sz_adj_p,
             sizeof(cd->UPD_BufRateAdjTabP_S8));
      memcpy(cd->UPD_BufRateAdjTabB_S8, anv_h264_brc_cbr_frm_sz_adj_b,
             sizeof(cd->UPD_BufRateAdjTabB_S8));
   } else {
      memcpy(cd->UPD_DistQPAdjTabI_U8, anv_h264_brc_vbr_dist_qp_adj_i,
             sizeof(cd->UPD_DistQPAdjTabI_U8));
      memcpy(cd->UPD_DistQPAdjTabP_U8, anv_h264_brc_vbr_dist_qp_adj_p,
             sizeof(cd->UPD_DistQPAdjTabP_U8));
      memcpy(cd->UPD_DistQPAdjTabB_U8, anv_h264_brc_vbr_dist_qp_adj_b,
             sizeof(cd->UPD_DistQPAdjTabB_U8));
      memcpy(cd->UPD_BufRateAdjTabI_S8, anv_h264_brc_vbr_frm_sz_adj_i,
             sizeof(cd->UPD_BufRateAdjTabI_S8));
      memcpy(cd->UPD_BufRateAdjTabP_S8, anv_h264_brc_vbr_frm_sz_adj_p,
             sizeof(cd->UPD_BufRateAdjTabP_S8));
      memcpy(cd->UPD_BufRateAdjTabB_S8, anv_h264_brc_vbr_frm_sz_adj_b,
             sizeof(cd->UPD_BufRateAdjTabB_S8));
   }

   memcpy(cd->UPD_FrmSzMinTabP_U8, anv_h264_brc_frm_sz_min_p,
          sizeof(cd->UPD_FrmSzMinTabP_U8));
   memcpy(cd->UPD_FrmSzMinTabI_U8, anv_h264_brc_frm_sz_min_i,
          sizeof(cd->UPD_FrmSzMinTabI_U8));
   memcpy(cd->UPD_FrmSzMaxTabP_U8, anv_h264_brc_frm_sz_max_p,
          sizeof(cd->UPD_FrmSzMaxTabP_U8));
   memcpy(cd->UPD_FrmSzMaxTabI_U8, anv_h264_brc_frm_sz_max_i,
          sizeof(cd->UPD_FrmSzMaxTabI_U8));
   memcpy(cd->UPD_FrmSzSCGTabP_U8, anv_h264_brc_frm_sz_scg_p,
          sizeof(cd->UPD_FrmSzSCGTabP_U8));
   memcpy(cd->UPD_FrmSzSCGTabI_U8, anv_h264_brc_frm_sz_scg_i,
          sizeof(cd->UPD_FrmSzSCGTabI_U8));

   memcpy(cd->UPD_I_IntraNonPred, anv_h264_brc_cost_i_intra_non_pred,
          sizeof(cd->UPD_I_IntraNonPred));
   memcpy(cd->UPD_I_Intra8x8, anv_h264_brc_cost_i_intra8x8,
          sizeof(cd->UPD_I_Intra8x8));
   memcpy(cd->UPD_I_Intra4x4, anv_h264_brc_cost_i_intra4x4,
          sizeof(cd->UPD_I_Intra4x4));
   memcpy(cd->UPD_P_IntraNonPred, anv_h264_brc_cost_p_intra_non_pred,
          sizeof(cd->UPD_P_IntraNonPred));
   memcpy(cd->UPD_P_Intra16x16, anv_h264_brc_cost_p_intra16x16,
          sizeof(cd->UPD_P_Intra16x16));
   memcpy(cd->UPD_P_Intra8x8, anv_h264_brc_cost_p_intra8x8,
          sizeof(cd->UPD_P_Intra8x8));
   memcpy(cd->UPD_P_Intra4x4, anv_h264_brc_cost_p_intra4x4,
          sizeof(cd->UPD_P_Intra4x4));
   memcpy(cd->UPD_P_Inter16x8, anv_h264_brc_cost_p_inter16x8,
          sizeof(cd->UPD_P_Inter16x8));
   memcpy(cd->UPD_P_Inter8x8, anv_h264_brc_cost_p_inter8x8,
          sizeof(cd->UPD_P_Inter8x8));
   memcpy(cd->UPD_P_Inter16x16, anv_h264_brc_cost_p_inter16x16,
          sizeof(cd->UPD_P_Inter16x16));
   memcpy(cd->UPD_P_RefId, anv_h264_brc_cost_p_ref_id, sizeof(cd->UPD_P_RefId));

#if GFX_VERx10 >= 125
   uint32_t gop_ref_dist = MAX2(vid->rc.consecutive_b_frames + 1, 1);
   uint32_t type;
   if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_I ||
       pic_type == STD_VIDEO_H264_PICTURE_TYPE_IDR)
      type = 0;
   else if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_P)
      type = gop_ref_dist == 1 ? 2 : 1;
   else
      type = 3;
   memcpy(cd->Reserved, h264_vdenc_brc_huc_const_reserved[type],
          sizeof(cd->Reserved));
#else
   for (uint32_t i = 0; i < 8; i++)
      for (uint32_t j = 0; j < 42; j++)
         cd->UPD_HMEMVCost[i][j] =
            anv_h264_brc_map44_lut(anv_h264_brc_hme_cost[i][j + 10], 0x6f);
#endif
}

static void
anv_h264_brc_fill_init_dmem(struct anv_cmd_buffer *cmd,
                            const StdVideoH264SequenceParameterSet *sps,
                            struct anv_huc_avc_brc_init_dmem *dmem)
{
   struct anv_video_session *vid = cmd->video.vid;
   struct anv_video_rc_state *rc = &vid->rc;
   bool is_cbr = vid->rc_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR;
   uint32_t width = (sps->pic_width_in_mbs_minus1 + 1) * 16;
   uint32_t height = (sps->pic_height_in_map_units_minus1 + 1) * 16;
   uint32_t frame_rate_num = rc->frame_rate_num ? rc->frame_rate_num : 30;
   uint32_t frame_rate_den = rc->frame_rate_den ? rc->frame_rate_den : 1;
   uint32_t frames_per_100s = frame_rate_num * 100 / frame_rate_den;
   if (frames_per_100s == 0)
      frames_per_100s = 3000;
   uint32_t target_bps = DIV_ROUND_UP(rc->average_bitrate, 1000) * 1000;
   uint32_t max_bps = is_cbr ? target_bps :
                      DIV_ROUND_UP(rc->max_bitrate, 1000) * 1000;
   uint32_t buf_size = rc->vbv_size_bits;
   uint32_t gop = rc->gop_frame_count ? rc->gop_frame_count : 30;
   uint32_t gop_ref_dist = MAX2(rc->consecutive_b_frames + 1, 1);

   dmem->BRCFunc_U8 = 0;
   dmem->INIT_BRCFlag_U16 =
      is_cbr ? ANV_HUC_AVC_BRC_FLAG_CBR : ANV_HUC_AVC_BRC_FLAG_VBR;
   dmem->INIT_FrameWidth_U16 = width;
   dmem->INIT_FrameHeight_U16 = height;
   dmem->INIT_TargetBitrate_U32 = target_bps;
   dmem->INIT_MaxRate_U32 = max_bps;
   /* Set the BRC min rate to the target bitrate for both CBR
    * (min = max = target) and VBR (min = target, max = maxrate). */
   dmem->INIT_MinRate_U32 = target_bps;
   dmem->INIT_BufSize_U32 = buf_size;
   dmem->INIT_InitBufFull_U32 = MIN2(rc->vbv_initial_fullness_bits, buf_size);
   /* TODO: proper profile/level based max frame size
    * This MB-count heuristic matches the media value at 720p/1080p. */
   dmem->INIT_ProfileLevelMaxFrame_U32 =
      (sps->pic_width_in_mbs_minus1 + 1) *
      (sps->pic_height_in_map_units_minus1 + 1) * 96;
   dmem->INIT_FrameRateM_U32 = frames_per_100s;
   dmem->INIT_FrameRateD_U32 = 100;

   uint32_t gop_p = gop > 1 ? (gop - 1) / gop_ref_dist : 0;
   uint32_t gop_b = gop_ref_dist > 1 ? (gop_ref_dist - 1) * gop_p : 0;
   dmem->INIT_GopP_U16 = gop_p;
   dmem->INIT_GopB_U16 = gop_b;
   dmem->INIT_MinQP_U16 = 10;
   dmem->INIT_MaxQP_U16 = 51;

   double bps_ratio = buf_size ? (double)max_bps / buf_size : 1.0;
   bps_ratio = CLAMP(bps_ratio, 0.1, 3.5);
   static const double pb_neg[4] = { 0.90, 0.66, 0.46, 0.3 };
   static const double pb_pos[4] = { 0.3, 0.46, 0.70, 0.90 };
   static const double i_neg[4] = { 0.80, 0.60, 0.34, 0.2 };
   static const double i_pos[4] = { 0.2, 0.4, 0.66, 0.9 };
   static const double vbr_neg[4] = { 0.90, 0.70, 0.50, 0.3 };
   static const double vbr_pos[4] = { 0.4, 0.5, 0.75, 0.90 };
   for (uint32_t i = 0; i < 4; i++) {
      dmem->INIT_DevThreshPB0_S8[i] = (int8_t)(-50.0 * pow(pb_neg[i], bps_ratio));
      dmem->INIT_DevThreshPB0_S8[i + 4] = (int8_t)(50.0 * pow(pb_pos[i], bps_ratio));
      dmem->INIT_DevThreshI0_S8[i] = (int8_t)(-50.0 * pow(i_neg[i], bps_ratio));
      dmem->INIT_DevThreshI0_S8[i + 4] = (int8_t)(50.0 * pow(i_pos[i], bps_ratio));
      dmem->INIT_DevThreshVBR0_S8[i] = (int8_t)(-50.0 * pow(vbr_neg[i], bps_ratio));
      dmem->INIT_DevThreshVBR0_S8[i + 4] = (int8_t)(100.0 * pow(vbr_pos[i], bps_ratio));
   }

   static const uint8_t est_rate_thresh[7] = { 4, 8, 12, 16, 20, 24, 28 };
   memcpy(dmem->INIT_EstRateThreshP0_U8, est_rate_thresh,
          sizeof(dmem->INIT_EstRateThreshP0_U8));
   memcpy(dmem->INIT_EstRateThreshB0_U8, est_rate_thresh,
          sizeof(dmem->INIT_EstRateThreshB0_U8));
   memcpy(dmem->INIT_EstRateThreshI0_U8, est_rate_thresh,
          sizeof(dmem->INIT_EstRateThreshI0_U8));

   int32_t qp = anv_brc_estimate_init_qp((double)(width * height * 3 / 2),
                                         frames_per_100s,
                                         (double)target_bps * 100.0);
   int32_t delta_q =
      (int32_t)(9 - (double)buf_size * frames_per_100s / ((double)target_bps * 100.0));
   qp += delta_q < 0 ? 0 : delta_q;
   qp = CLAMP(qp, 1, 51);
   qp--;
   if (qp < 0)
      qp = 1;
   dmem->INIT_InitQPIP = qp;

   /* Enable fractional QP (extended rho domain).
    * The BRC update's rate model relies on it.
    */
   dmem->INIT_FracQPEnable_U8 = 1;
   dmem->INIT_QPSelectForFirstPass_U8 = 1;
   dmem->INIT_MBHeaderCompensation_U8 = 1;
   dmem->INIT_DeltaQP_Adaptation_U8 = 1;
   dmem->INIT_MaxCRFQualityFactor_U8 = 52;
   dmem->INIT_TopQPDeltaThrForAdapt2Pass_U8 = 2;
   dmem->INIT_BotQPDeltaThrForAdapt2Pass_U8 = 1;
   dmem->INIT_TopFrmSzThrForAdapt2Pass_U8 = 32;
   dmem->INIT_BotFrmSzThrForAdapt2Pass_U8 = 24;
}

static void
anv_h264_brc_fill_update_dmem(struct anv_cmd_buffer *cmd,
                              const StdVideoH264SequenceParameterSet *sps,
                              StdVideoH264PictureType pic_type,
                              uint32_t num_slices,
                              uint32_t pass,
                              struct anv_huc_avc_brc_update_dmem *dmem)
{
   struct anv_video_session *vid = cmd->video.vid;
   struct anv_video_rc_state *rc = &vid->rc;
   uint32_t frame_rate_num = rc->frame_rate_num ? rc->frame_rate_num : 30;
   uint32_t frame_rate_den = rc->frame_rate_den ? rc->frame_rate_den : 1;
   uint32_t frames_per_100s = frame_rate_num * 100 / frame_rate_den;
   if (frames_per_100s == 0)
      frames_per_100s = 3000;
   uint32_t target_bps = DIV_ROUND_UP(rc->average_bitrate, 1000) * 1000;
   uint32_t input_bits_per_frame = frames_per_100s ?
      (uint32_t)((uint64_t)target_bps * 100 / frames_per_100s) : 0;
   uint32_t gop = rc->gop_frame_count ? rc->gop_frame_count : 30;
   uint32_t gop_ref_dist = MAX2(rc->consecutive_b_frames + 1, 1);
   uint32_t num_p = gop > 1 ? (gop - 1) / gop_ref_dist : 0;
   uint8_t scene_chg_width = MIN2((num_p + 1) / 5, 6);

   dmem->BRCFunc_U8 = 1;
   uint64_t target_acc =
      (uint64_t)MIN2(rc->vbv_initial_fullness_bits, rc->vbv_size_bits) +
      (uint64_t)rc->frame_counter * input_bits_per_frame;
   dmem->UPD_TARGETSIZE_U32 = rc->vbv_size_bits ?
      (uint32_t)(target_acc % rc->vbv_size_bits) : (uint32_t)target_acc;

   dmem->UPD_FRAMENUM_U32 = rc->frame_counter;
   dmem->UPD_PeakTxBitsPerFrame_U32 = input_bits_per_frame;

   dmem->UPD_CurrFrameType_U8 =
      (pic_type == STD_VIDEO_H264_PICTURE_TYPE_I ||
       pic_type == STD_VIDEO_H264_PICTURE_TYPE_IDR) ? 2 :
      (pic_type == STD_VIDEO_H264_PICTURE_TYPE_P) ? 0 : 1;

   dmem->UPD_WidthInMB_U16 = sps->pic_width_in_mbs_minus1 + 1;
   dmem->UPD_HeightInMB_U16 = sps->pic_height_in_map_units_minus1 + 1;
   dmem->UPD_SLBB_Size_U16 = ANV_H264_BRC_SLB_SIZE;
   dmem->UPD_AvcImgStateOffset_U16 = 0;
   dmem->UPD_PAKPassNum_U8 = pass;
   dmem->UPD_MaxNumPass_U8 = 2;

   static const uint16_t start_gadj_frame[4] = { 10, 50, 100, 150 };
   static const uint8_t start_gadj_mult[5] = { 1, 1, 3, 2, 1 };
   static const uint8_t start_gadj_div[5] = { 40, 5, 5, 3, 1 };
   static const uint8_t rate_ratio_threshold[7] = { 80, 90, 95, 101, 105, 115, 130 };
   static const int8_t rate_ratio_threshold_qp[8] = { -3, -2, -1, 0, 1, 1, 2, 3 };
   memcpy(dmem->UPD_startGAdjFrame_U16, start_gadj_frame, sizeof(start_gadj_frame));
   memcpy(dmem->UPD_startGAdjMult_U8, start_gadj_mult, sizeof(start_gadj_mult));
   memcpy(dmem->UPD_startGAdjDiv_U8, start_gadj_div, sizeof(start_gadj_div));
   memcpy(dmem->UPD_gRateRatioThreshold_U8, rate_ratio_threshold, sizeof(rate_ratio_threshold));
   memcpy(dmem->UPD_gRateRatioThresholdQP_U8, rate_ratio_threshold_qp, sizeof(rate_ratio_threshold_qp));

   dmem->UPD_SceneChgWidth_U8[0] = scene_chg_width;
   dmem->UPD_SceneChgWidth_U8[1] = scene_chg_width;
   dmem->UPD_SceneChgDetectEn_U8 = 1;
   dmem->UPD_SceneChgPrevIntraPctThreshold_U8 = 96;
   dmem->UPD_SceneChgCurIntraPctThreshold_U8 = 192;
   dmem->UPD_IPAverageCoeff_U8 = 128;
   dmem->UPD_HMECostEnable_U8 = 1;

   (void)num_slices;
}

/* Defined in genX_cmd_video_enc.c; the BRC input SLB reuses the same image
 * state emission as the CQP encode path.
 */
static void
anv_video_emit_mi_flush_dw(struct anv_cmd_buffer *cmd, bool cache_invalidate);
static void
anv_vdenc_emit_vd_pipeline_flush(struct anv_cmd_buffer *cmd);

static void
anv_h264_emit_mfx_avc_img_state(struct anv_cmd_buffer *cmd,
                                struct anv_batch *batch,
                                const VkVideoEncodeInfoKHR *enc_info,
                                bool brc_enabled);
static void
anv_h264_emit_vdenc_img_state(struct anv_cmd_buffer *cmd,
                              struct anv_batch *batch,
                              const VkVideoEncodeInfoKHR *enc_info);

#if GFX_VERx10 >= 125
static void
anv_h264_emit_vdenc_cmd3(struct anv_cmd_buffer *cmd,
                         struct anv_batch *batch,
                         const VkVideoEncodeInfoKHR *enc_info);

static void
anv_h264_emit_vdenc_avc_img_state(struct anv_cmd_buffer *cmd,
                                  struct anv_batch *batch,
                                  const VkVideoEncodeInfoKHR *enc_info,
                                  const uint8_t *dpb_idx);
#endif

static struct anv_address
anv_brc_mem_addr(struct anv_video_session *vid, uint32_t mem_idx)
{
   return (struct anv_address) {
      vid->vid_mem[mem_idx].mem->bo,
      vid->vid_mem[mem_idx].offset,
   };
}

/* Build the SLBB input read buffer the HuC BRC Update kernel parses and
 * patches: the H.264 image-state group (MFX_AVC_IMG_STATE + VDENC image state
 * + MI_BATCH_BUFFER_END). The HuC copies it to the output SLBB while writing
 * the BRC-computed frame QP into VDENC_IMG_STATE.QPPRIMEY.
 */
static void
anv_h264_brc_build_input_slb(struct anv_cmd_buffer *cmd,
                             const VkVideoEncodeInfoKHR *enc_info,
                             const uint8_t *dpb_idx,
                             struct anv_state slb_state)
{
   struct anv_batch slb = { 0 };

   anv_batch_set_storage(&slb,
                         anv_cmd_buffer_temporary_state_address(cmd, slb_state),
                         slb_state.map, slb_state.alloc_size);
   memset(slb_state.map, 0, slb_state.alloc_size);

   anv_h264_emit_mfx_avc_img_state(cmd, &slb, enc_info, true);
#if GFX_VERx10 >= 125
   anv_h264_emit_vdenc_cmd3(cmd, &slb, enc_info);
   anv_h264_emit_vdenc_avc_img_state(cmd, &slb, enc_info, dpb_idx);
#else
   anv_h264_emit_vdenc_img_state(cmd, &slb, enc_info);
#endif
   anv_batch_emit(&slb, GENX(MI_BATCH_BUFFER_END), bbe);

   assert((uint32_t)(slb.next - slb.start) == ANV_H264_BRC_SLB_SIZE);
}

#define ANV_HUC_AVC_BRC_REENCODE_MASK  (1u << 31)
#define ANV_HUC_AVC_BRC_ERROR_MASK     (1u << 26)

/* MFC PAK output MMIO register offsets (from the CS MMIO base, used with
 * MI_STORE_REGISTER_MEM AddCSMMIOStartOffset). DG2 (gen125) inherits the gen12
 * MFX class and does not remap these, so the same values apply to both
 * generations. */
#define ANV_H264_MFC_BITSTREAM_BYTECOUNT_FRAME_OFFSET 0x08A0
#define ANV_H264_MFC_IMAGE_STATUS_CTRL_OFFSET         0x08B8
#define ANV_H264_MFC_AVC_NUM_SLICES_OFFSET            0x0954

/* Update DMEM byte offsets of the PAK-output fields (see anv_huc_avc_brc_update_dmem). */
#define ANV_H264_BRC_DMEM_FRAME_BYTE_COUNT_OFFSET 20
#define ANV_H264_BRC_DMEM_IMAGE_STATUS_CTRL_OFFSET 28
#define ANV_H264_BRC_DMEM_NUM_OF_SLICE_OFFSET     384

static void
anv_h264_brc_emit_huc_common(struct anv_cmd_buffer *cmd,
                             uint32_t kernel_descriptor,
                             struct anv_address dmem_addr,
                             uint32_t dmem_size)
{
   struct anv_device *device = cmd->device;

   anv_batch_emit(&cmd->batch, GENX(HUC_IMEM_STATE), imem) {
      imem.HUCFirmwareDescriptor = kernel_descriptor;
   }

   anv_batch_emit(&cmd->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }
   anv_batch_emit(&cmd->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }

   anv_batch_emit(&cmd->batch, GENX(HUC_PIPE_MODE_SELECT), sel);

   anv_batch_emit(&cmd->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }

   anv_batch_emit(&cmd->batch, GENX(HUC_DMEM_STATE), dmem) {
      dmem.HUCDataSourceAddress = dmem_addr;
      dmem.HUCDataSourceAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(device, dmem_addr.bo, 0),
      };
      dmem.HUCDataDestinationAddress = (struct anv_address) {
         NULL, ANV_HUC_DMEM_DEST_OFFSET,
      };
      dmem.HUCDataLength = align(dmem_size, 64) / 64;
   }
}

static void
anv_h264_brc_emit_huc_flush_and_status(struct anv_cmd_buffer *cmd)
{
   struct anv_video_session *vid = cmd->video.vid;
   struct anv_address pak_mmio_addr =
      anv_brc_mem_addr(vid, ANV_VID_MEM_BRC_PAK_MMIO_SEM);

   anv_batch_emit(&cmd->batch, GENX(VD_PIPELINE_FLUSH), flush) {
      flush.HEVCPipelineDone = true;
      flush.HEVCPipelineCommandFlush = true;
      flush.VDCommandMessageParserDone = true;
   }

   anv_video_emit_mi_flush_dw(cmd, true);

   anv_batch_emit(&cmd->batch, GENX(MI_STORE_DATA_IMM), sdi) {
      sdi.Address = anv_address_add(pak_mmio_addr, 4);
      sdi.ImmediateData = ANV_HUC_AVC_BRC_REENCODE_MASK;
   }

   anv_batch_emit(&cmd->batch, GENX(MI_STORE_REGISTER_MEM), srm) {
      srm.RegisterAddress = ANV_HUC_STATUS_MMIO_OFFSET;
      srm.AddCSMMIOStartOffset = 1;
      srm.MemoryAddress = pak_mmio_addr;
   }

   struct anv_address huc_err_addr =
      anv_brc_mem_addr(vid, ANV_VID_MEM_BRC_HUC_ERR_SEM);

   anv_batch_emit(&cmd->batch, GENX(MI_STORE_DATA_IMM), sdi) {
      sdi.Address = anv_address_add(huc_err_addr, 4);
      sdi.ImmediateData = ANV_HUC_AVC_BRC_ERROR_MASK;
   }

   anv_batch_emit(&cmd->batch, GENX(MI_STORE_REGISTER_MEM), srm) {
      srm.RegisterAddress = ANV_HUC_STATUS_MMIO_OFFSET;
      srm.AddCSMMIOStartOffset = 1;
      srm.MemoryAddress = huc_err_addr;
   }

   anv_batch_emit(&cmd->batch, GENX(MI_CONDITIONAL_BATCH_BUFFER_END), cbbe) {
      cbbe.CompareOperation = MADEqualIDD;
      cbbe.CompareSemaphore = 1;
      cbbe.CompareMaskMode = CompareMaskModeEnabled;
      cbbe.CompareDataDword = 0;
      cbbe.CompareAddress = huc_err_addr;
   }
}

static void
anv_h264_brc_emit_huc_tail(struct anv_cmd_buffer *cmd)
{
   struct anv_state status_state =
      anv_cmd_buffer_alloc_temporary_state(cmd, 8, 8);

   if (status_state.map == NULL)
      return;

   struct anv_address status_addr =
      anv_cmd_buffer_temporary_state_address(cmd, status_state);

   anv_batch_emit(&cmd->batch, GENX(MI_STORE_REGISTER_MEM), srm) {
      srm.RegisterAddress = ANV_HUC_STATUS2_MMIO_OFFSET;
      srm.AddCSMMIOStartOffset = 1;
      srm.MemoryAddress = status_addr;
   }

   anv_batch_emit(&cmd->batch, GENX(HUC_START), start) {
      start.LastStreamObject = true;
   }

   anv_h264_brc_emit_huc_flush_and_status(cmd);
}

static void
anv_h264_brc_emit_huc_init(struct anv_cmd_buffer *cmd,
                           const struct VkVideoEncodeH264PictureInfoKHR *frame_info)
{
   const StdVideoH264SequenceParameterSet *sps = vk_video_find_h264_enc_std_sps(
         cmd->video.params,
         frame_info->pStdPictureInfo->seq_parameter_set_id);
   struct anv_device *device = cmd->device;
   struct anv_video_session *vid = cmd->video.vid;
   uint32_t dmem_size = sizeof(struct anv_huc_avc_brc_init_dmem);
   struct anv_state dmem_state =
      anv_cmd_buffer_alloc_temporary_state(cmd, dmem_size, 4096);

   if (dmem_state.map == NULL)
      return;

   memset(dmem_state.map, 0, dmem_size);
   anv_h264_brc_fill_init_dmem(cmd, sps, dmem_state.map);

   anv_h264_brc_emit_huc_common(cmd, ANV_HUC_AVC_BRC_INIT_KERNEL_DESCRIPTOR,
                                anv_cmd_buffer_temporary_state_address(cmd, dmem_state),
                                dmem_size);

   anv_batch_emit(&cmd->batch, GENX(HUC_VIRTUAL_ADDR_STATE), va) {
      va.HUCVirtualAddressRegion[0] = (struct GENX(HUC_VIRTUAL_ADDR_REGION)) {
         .Address = anv_brc_mem_addr(vid, ANV_VID_MEM_BRC_HISTORY),
         .AddressAttributes = {
            .MOCS = anv_mocs(device, NULL, 0),
         },
      };

      for (uint32_t i = 1; i < 16; i++) {
         va.HUCVirtualAddressRegion[i].AddressAttributes =
            (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(device, NULL, 0),
         };
      }
   }

   anv_h264_brc_emit_huc_tail(cmd);
}

static void
anv_h264_brc_emit_huc_update(struct anv_cmd_buffer *cmd,
                             const struct VkVideoEncodeH264PictureInfoKHR *frame_info,
                             uint32_t pass,
                             struct anv_state input_slb_state)
{
   const StdVideoH264SequenceParameterSet *sps = vk_video_find_h264_enc_std_sps(
         cmd->video.params,
         frame_info->pStdPictureInfo->seq_parameter_set_id);
   const uint32_t num_slices = frame_info->naluSliceEntryCount;
   const uint32_t pic_type = frame_info->pStdPictureInfo->primary_pic_type;

   struct anv_device *device = cmd->device;
   struct anv_video_session *vid = cmd->video.vid;
   uint32_t dmem_size = sizeof(struct anv_huc_avc_brc_update_dmem);
   uint32_t cd_size = sizeof(struct anv_huc_avc_brc_const_data);

   struct anv_state dmem_state =
      anv_cmd_buffer_alloc_temporary_state(cmd, dmem_size, 4096);
   struct anv_state cd_state =
      anv_cmd_buffer_alloc_temporary_state(cmd, cd_size, 4096);

   if (dmem_state.map == NULL || cd_state.map == NULL)
      return;

   memset(dmem_state.map, 0, dmem_size);
   anv_h264_brc_fill_update_dmem(cmd, sps, pic_type, num_slices, pass,
                                 dmem_state.map);

   /* Overlay the previous frame's PAK MMIO outputs (frame byte count, image
    * status, slice count) that were staged into the persistent PAK info
    * buffer after the previous PAK, so the HuC BRC Update sees the real prior
    * frame size and can drive the bitrate. */
   struct mi_builder b;
   mi_builder_init(&b, cmd->device->info, &cmd->batch);
   struct anv_address dmem_addr =
      anv_cmd_buffer_temporary_state_address(cmd, dmem_state);
   struct anv_address pak_info =
      anv_brc_mem_addr(vid, ANV_VID_MEM_BRC_PAK_INFO);
   mi_memcpy(&b, anv_address_add(dmem_addr, ANV_H264_BRC_DMEM_FRAME_BYTE_COUNT_OFFSET),
             pak_info, 4);
   mi_memcpy(&b, anv_address_add(dmem_addr, ANV_H264_BRC_DMEM_IMAGE_STATUS_CTRL_OFFSET),
             anv_address_add(pak_info, 4), 4);
   mi_memcpy(&b, anv_address_add(dmem_addr, ANV_H264_BRC_DMEM_NUM_OF_SLICE_OFFSET),
             anv_address_add(pak_info, 8), 4);

   /* The mi_memcpy above patches the update DMEM via the command streamer.
    * Flush so those writes are visible to the HuC DMEM DMA read that follows. */
   anv_video_emit_mi_flush_dw(cmd, true);

   memset(cd_state.map, 0, cd_size);
   anv_h264_brc_fill_const_data(vid, pic_type, cd_state.map);

   anv_h264_brc_emit_huc_common(cmd, ANV_HUC_AVC_BRC_UPDATE_KERNEL_DESCRIPTOR,
                                anv_cmd_buffer_temporary_state_address(cmd, dmem_state),
                                dmem_size);

   anv_batch_emit(&cmd->batch, GENX(HUC_VIRTUAL_ADDR_STATE), va) {
      struct anv_address regions[16] = { 0 };

      regions[0] = anv_brc_mem_addr(vid, ANV_VID_MEM_BRC_HISTORY);
      regions[1] = anv_brc_mem_addr(vid, ANV_VID_MEM_BRC_VDENC_STATS);
      regions[2] = anv_brc_mem_addr(vid, ANV_VID_MEM_BRC_PAK_STATS);
      regions[3] = anv_cmd_buffer_temporary_state_address(cmd, input_slb_state);
      regions[5] = anv_cmd_buffer_temporary_state_address(cmd, cd_state);
      regions[6] = anv_brc_mem_addr(vid, ANV_VID_MEM_BRC_EXEC_SLB);
      regions[15] = anv_brc_mem_addr(vid, ANV_VID_MEM_BRC_DEBUG);

      for (uint32_t i = 0; i < 16; i++) {
         va.HUCVirtualAddressRegion[i] = (struct GENX(HUC_VIRTUAL_ADDR_REGION)) {
            .Address = regions[i],
            .AddressAttributes = {
               .MOCS = anv_mocs(device, regions[i].bo, 0),
            },
         };
      }
   }

   anv_h264_brc_emit_huc_tail(cmd);
}
#endif /* GFX_VER >= 12 */


#endif /* GENX_HUC_BRC_H */
