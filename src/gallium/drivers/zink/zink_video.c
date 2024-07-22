/*
 * Copyright © 2021 Red Hat
 * Copyright © 2021 Valve Corporation
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
 *
 * Authors:
 *    Dave Airlie <airlied@redhat.com>
 *    Mike Blumenkrantz <michael.blumenkrantz@gmail.com>
 */

#include "zink_context.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_surface.h"
#include "zink_video.h"

#include "util/stack_array.h"
#include "util/u_video.h"
#include "pipe/p_video_codec.h"
#include "vl/vl_video_buffer.h"

#include "util/vl_zscan_data.h"

#ifndef _WIN32
#include "drm-uapi/drm_fourcc.h"
#else
/* these won't actually be used */
#define DRM_FORMAT_MOD_INVALID 0
#define DRM_FORMAT_MOD_LINEAR 0
#endif

/* also unwinds a partially created session */
static void
destroy_session(struct zink_video_codec *zvc)
{
   struct zink_screen *screen = zink_screen(zvc->screen);

   for (unsigned i = 0; i < ARRAY_SIZE(zvc->dpb_res); i++) {
      VKSCR(DestroyImageView)(screen->dev, zvc->dpb_resources[i].imageViewBinding, NULL);
      zvc->dpb_resources[i].imageViewBinding = VK_NULL_HANDLE;
      pipe_resource_reference(&zvc->dpb_res[i], NULL);
   }
   for (unsigned i = 0; i < zvc->num_priv_mems; i++) {
      if (zvc->priv_mems[i])
         zink_bo_unref(screen, zvc->priv_mems[i]);
   }
   free(zvc->priv_mems);
   zvc->priv_mems = NULL;
   zvc->num_priv_mems = 0;
   VKSCR(DestroyVideoSessionKHR)(screen->dev, zvc->session, NULL);
   zvc->session = VK_NULL_HANDLE;
   zvc->reset_sent = false;
}

static void
zink_destroy_video_codec(struct pipe_video_codec *codec) {
   struct zink_video_codec *zvc = (struct zink_video_codec *)codec;
   struct zink_screen *screen = (struct zink_screen *)zvc->screen;

   for (unsigned i = 0; i < NUM_BUFFERS; i++)
      pipe_resource_reference(&zvc->bs[i], NULL);

   for (unsigned i = 0; i < 17; i++) {
      if (zvc->render_pic_list[i])
         vl_video_buffer_set_associated_data(zvc->render_pic_list[i], &zvc->base, NULL, NULL);
   }
   /* ensure session is no longer in use */
   VKSCR(QueueWaitIdle)(screen->queue_video_decode);
   VKSCR(DestroyCommandPool)(screen->dev, zvc->cmdpool, NULL);
   destroy_session(zvc);
   free(zvc);
}

static void
zink_destroy_associated_data(void *data)
{
   struct zink_video_surf_data *surf = data;
   struct zink_video_codec *zvc = (struct zink_video_codec *)surf->codec;
   struct zink_screen *screen = (struct zink_screen *)zvc->screen;
   VKSCR(DestroyImageView)(screen->dev, surf->resource.imageViewBinding, NULL);

   for (unsigned i = 0; i < zvc->max_dpb_slots; i++) {
      if (zvc->render_pic_list[i] == data) {
         zvc->render_pic_list[i] = NULL;
      }
   }
   /* destroy iv */
   free(surf);
}

static bool
bind_session_memory(struct zink_video_codec *zvc)
{
   struct zink_screen *screen = zink_screen(zvc->screen);
   uint32_t mem_req_count = 0;

   if (VKSCR(GetVideoSessionMemoryRequirementsKHR)(screen->dev, zvc->session, &mem_req_count, NULL) != VK_SUCCESS)
      return false;
   if (!mem_req_count)
      return true;
   zvc->priv_mems = calloc(mem_req_count, sizeof(struct zink_bo *));
   if (!zvc->priv_mems)
      return false;
   zvc->num_priv_mems = mem_req_count;

   STACK_ARRAY(VkVideoSessionMemoryRequirementsKHR, session_memory_reqs, mem_req_count);
   STACK_ARRAY(VkBindVideoSessionMemoryInfoKHR, bind_memory, mem_req_count);
   bool ok = session_memory_reqs && bind_memory;
   if (ok) {
      for (unsigned i = 0; i < mem_req_count; i++)
         session_memory_reqs[i] = (VkVideoSessionMemoryRequirementsKHR){ VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR };
      ok = VKSCR(GetVideoSessionMemoryRequirementsKHR)(screen->dev, zvc->session,
                                                       &mem_req_count, session_memory_reqs) == VK_SUCCESS;
   }

   static const enum zink_heap heaps[] = {
      ZINK_HEAP_DEVICE_LOCAL,
      ZINK_HEAP_DEVICE_LOCAL_VISIBLE,
      ZINK_HEAP_HOST_VISIBLE_COHERENT,
      ZINK_HEAP_HOST_VISIBLE_COHERENT_CACHED,
   };
   for (unsigned i = 0; ok && i < mem_req_count; i++) {
      const VkMemoryRequirements *reqs = &session_memory_reqs[i].memoryRequirements;
      for (unsigned h = 0; !zvc->priv_mems[i] && h < ARRAY_SIZE(heaps); h++) {
         enum zink_heap heap = heaps[h];
         for (unsigned j = 0; !zvc->priv_mems[i] && j < screen->heap_count[heap]; j++) {
            if (!(reqs->memoryTypeBits & BITFIELD_BIT(screen->heap_map[heap][j])))
               continue;
            zvc->priv_mems[i] = zink_bo(zink_bo_create(screen, reqs->size, reqs->alignment, heap, ZINK_ALLOC_NO_SUBALLOC, screen->heap_map[heap][j], NULL, NULL));
         }
      }
      if (!zvc->priv_mems[i]) {
         ok = false;
         break;
      }
      bind_memory[i].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
      bind_memory[i].pNext = NULL;
      bind_memory[i].memoryBindIndex = session_memory_reqs[i].memoryBindIndex;
      bind_memory[i].memory = zink_bo_get_mem(zvc->priv_mems[i]);
      bind_memory[i].memoryOffset = 0;
      bind_memory[i].memorySize = reqs->size;
   }
   if (ok)
      ok = VKSCR(BindVideoSessionMemoryKHR)(screen->dev, zvc->session, mem_req_count, bind_memory) == VK_SUCCESS;

   STACK_ARRAY_FINISH(session_memory_reqs);
   STACK_ARRAY_FINISH(bind_memory);
   return ok;
}

static bool
zink_video_create_session(struct zink_video_codec *zvc,
                          uint32_t width, uint32_t height,
                          enum pipe_format format,
                          enum pipe_video_profile profile,
                          enum pipe_video_entrypoint entrypoint)
{
   struct zink_screen *screen = (struct zink_screen *)zvc->base.context->screen;
   uint32_t bit_depth = zink_video_get_format_bit_depth(format);
   struct zink_video_profile vk_profile = { 0 };
   if (!zink_video_fill_single_profile(screen, profile, bit_depth, &vk_profile))
      return false;

   struct zink_video_caps_info caps_info = { 0 };
   if (!zink_video_fill_caps(screen, profile, entrypoint, bit_depth != 8, &caps_info))
      return false;

   zvc->max_dpb_slots = MIN2(caps_info.caps.maxDpbSlots, ARRAY_SIZE(zvc->dpb_res));
   zvc->coincide_dpb = caps_info.dec_caps.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;
   zvc->dpb_array = !(caps_info.caps.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR);
   if (!zvc->coincide_dpb) {
      struct pipe_resource dpb_templ = {};
      struct pipe_surface psurf = {};
      dpb_templ.width0 = width;
      dpb_templ.height0 = height;
      dpb_templ.depth0 = 1;
      dpb_templ.format = format;
      dpb_templ.target = PIPE_TEXTURE_2D;
      dpb_templ.usage = PIPE_USAGE_DEFAULT;
      dpb_templ.flags = ZINK_RESOURCE_FLAG_VIDEO_DPB | PIPE_RESOURCE_FLAG_DONT_MAP_DIRECTLY | ZINK_RESOURCE_FLAG_INTERNAL_ONLY;
      dpb_templ.bind = ZINK_BIND_VIDEO;
      dpb_templ.array_size = 1;

      psurf.format = format;

      if (zvc->dpb_array) {
         dpb_templ.array_size = zvc->max_dpb_slots;
         dpb_templ.target = PIPE_TEXTURE_2D_ARRAY;
         zvc->dpb_res[0] = screen->base.resource_create(&screen->base,
                                                        &dpb_templ);
         if (!zvc->dpb_res[0])
            goto fail;
         for (unsigned i = 0; i < zvc->max_dpb_slots; i++) {
            psurf.first_layer = psurf.last_layer = i;
            VkImageViewCreateInfo ivci = create_ivci(screen, zink_resource(zvc->dpb_res[0]),
                                                     &psurf, PIPE_TEXTURE_2D);
            zvc->dpb_resources[i].sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
            zvc->dpb_resources[i].pNext = NULL;
            zvc->dpb_resources[i].codedExtent.width = width;
            zvc->dpb_resources[i].codedExtent.height = height;
            if (VKSCR(CreateImageView)(screen->dev, &ivci, NULL, &zvc->dpb_resources[i].imageViewBinding) != VK_SUCCESS)
               goto fail;
         }
      } else {
         for (unsigned i = 0; i < zvc->max_dpb_slots; i++) {
            zvc->dpb_res[i] = screen->base.resource_create(&screen->base,
                                                           &dpb_templ);
            if (!zvc->dpb_res[i])
               goto fail;
            zvc->dpb_resources[i].sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
            zvc->dpb_resources[i].pNext = NULL;
            zvc->dpb_resources[i].codedExtent.width = width;
            zvc->dpb_resources[i].codedExtent.height = height;
            VkImageViewCreateInfo ivci = create_ivci(screen, zink_resource(zvc->dpb_res[i]),
                                                     &psurf, PIPE_TEXTURE_2D);
            if (VKSCR(CreateImageView)(screen->dev, &ivci, NULL, &zvc->dpb_resources[i].imageViewBinding) != VK_SUCCESS)
               goto fail;
         }
      }
   }

   const VkExtensionProperties h264_props = {
      VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME,
      VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION
   };

   VkVideoSessionCreateInfoKHR sci = { 0 };
   sci.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
   sci.queueFamilyIndex = screen->video_decode_queue;
   sci.pVideoProfile = &vk_profile.profile;
   sci.maxCodedExtent.width = width;
   sci.maxCodedExtent.height = height;
   sci.pictureFormat = zink_get_format(screen, format);
   sci.referencePictureFormat = zink_get_format(screen, format);
   sci.maxActiveReferencePictures = caps_info.caps.maxActiveReferencePictures;
   sci.maxDpbSlots = zvc->max_dpb_slots;
   sci.pStdHeaderVersion = &h264_props;

   if (VKSCR(CreateVideoSessionKHR)(screen->dev, &sci, NULL, &zvc->session) != VK_SUCCESS)
      goto fail;
   if (!bind_session_memory(zvc))
      goto fail;

   zvc->srcbuf_align = caps_info.caps.minBitstreamBufferSizeAlignment;
   return true;

fail:
   mesa_loge("ZINK: failed to create video session");
   destroy_session(zvc);
   return false;
}

static void
zink_begin_frame(struct pipe_video_codec *codec,
                 struct pipe_video_buffer *target,
                 struct pipe_picture_desc *picture)
{
   struct zink_video_codec *zvc = (struct zink_video_codec *)codec;
   zvc->bs_size = 0;
   zvc->bs_ptr = pipe_buffer_map(zvc->base.context, zvc->bs[zvc->cur_bs_buf], PIPE_MAP_WRITE, &zvc->bs_xfer);

   zvc->error = !zvc->session &&
                !zink_video_create_session(zvc, codec->width, codec->height,
                                           target->buffer_format,
                                           codec->profile, codec->entrypoint);
}

static void
zink_decode_macroblock(struct pipe_video_codec *codec,
                       struct pipe_video_buffer *target,
                       struct pipe_picture_desc *picture,
                       const struct pipe_macroblock *macroblocks,
                       unsigned num_macroblocks)
{
}

static void
zink_decode_bitstream(struct pipe_video_codec *codec,
                        struct pipe_video_buffer *target,
                        struct pipe_picture_desc *picture,
                        unsigned num_buffers,
                        const void * const *buffers,
                        const unsigned *sizes)
{
   struct zink_video_codec *zvc = (struct zink_video_codec *)codec;
   for (unsigned i = 0; i < num_buffers; ++i) {
      memcpy(zvc->bs_ptr, buffers[i], sizes[i]);
      zvc->bs_size += sizes[i];
      zvc->bs_ptr += sizes[i];
   }
}


static StdVideoH264LevelIdc
h264_level_conv(unsigned level_idc)
{
   static const uint8_t levels[] = {10, 11, 12, 13, 20, 21, 22, 30, 31, 32, 40, 41, 42, 50, 51, 52, 60, 61, 62};

   if (level_idc == 9)
      return STD_VIDEO_H264_LEVEL_IDC_1_1;
   for (unsigned i = 0; i < ARRAY_SIZE(levels); i++) {
      if (levels[i] >= level_idc)
         return i;
   }
   return STD_VIDEO_H264_LEVEL_IDC_6_2;
}

static void
convert_pps_sps(struct pipe_picture_desc *picture,
                unsigned level,
                StdVideoH264SequenceParameterSet *vsps,
                StdVideoH264PictureParameterSet *vpps,
                StdVideoH264ScalingLists *pps_scaling_list)
{
   struct pipe_h264_picture_desc *h264 = (struct pipe_h264_picture_desc *)picture;
   struct pipe_h264_pps *pps = h264->pps;
   struct pipe_h264_sps *sps = pps->sps;

   vpps->flags.transform_8x8_mode_flag = pps->transform_8x8_mode_flag;
   vpps->flags.redundant_pic_cnt_present_flag = pps->redundant_pic_cnt_present_flag;
   vpps->flags.constrained_intra_pred_flag = pps->constrained_intra_pred_flag;
   vpps->flags.deblocking_filter_control_present_flag = pps->deblocking_filter_control_present_flag;
   vpps->flags.weighted_pred_flag = pps->weighted_pred_flag;
   vpps->flags.entropy_coding_mode_flag = pps->entropy_coding_mode_flag;
   vpps->flags.pic_scaling_matrix_present_flag = true;
   vpps->flags.bottom_field_pic_order_in_frame_present_flag = pps->bottom_field_pic_order_in_frame_present_flag;

   vpps->num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
   vpps->num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;
   vpps->weighted_bipred_idc = pps->weighted_bipred_idc;
   vpps->pic_init_qp_minus26 = pps->pic_init_qp_minus26;
//   vpps->pic_init_qs_minus26;
   vpps->chroma_qp_index_offset = pps->chroma_qp_index_offset;
   vpps->second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset;

   pps_scaling_list->scaling_list_present_mask = 0xff;

   /* have to convert scaling matrix through zscan */
   for (unsigned m = 0; m < 6; m++)
      for (unsigned q = 0; q < 16; q++)
         pps_scaling_list->ScalingList4x4[m][q] = pps->ScalingList4x4[m][vl_zscan_normal_16[q]];

   for (unsigned m = 0; m < 6; m++)
      for (unsigned q = 0; q < 64; q++)
         pps_scaling_list->ScalingList8x8[m][q] = pps->ScalingList8x8[m][vl_zscan_normal[q]];


   switch (h264->base.profile) {
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
     vsps->profile_idc = STD_VIDEO_H264_PROFILE_IDC_BASELINE;
     break;
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
     vsps->profile_idc = STD_VIDEO_H264_PROFILE_IDC_MAIN;
     break;
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
     vsps->profile_idc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
     break;
   default:
     assert(0);
   }
   vsps->level_idc = h264_level_conv(sps->level_idc ? sps->level_idc : level);

   vsps->flags.direct_8x8_inference_flag = sps->direct_8x8_inference_flag;
   vsps->flags.mb_adaptive_frame_field_flag = sps->mb_adaptive_frame_field_flag;
   vsps->flags.frame_mbs_only_flag = sps->frame_mbs_only_flag;
   vsps->flags.delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag;
   vsps->flags.separate_colour_plane_flag = sps->separate_colour_plane_flag;

   vsps->bit_depth_luma_minus8 = sps->bit_depth_luma_minus8;
   vsps->bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8;
   vsps->log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4;
   vsps->pic_order_cnt_type = sps->pic_order_cnt_type;
   vsps->log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4;
   vsps->max_num_ref_frames = h264->num_ref_frames;
   vsps->chroma_format_idc = sps->chroma_format_idc;
   vsps->pic_width_in_mbs_minus1 = sps->pic_width_in_mbs_minus1;
   /* no interlace support */
   vsps->pic_height_in_map_units_minus1 = sps->pic_height_in_mbs_minus1;
}

static struct zink_video_surf_data *
create_surf(struct zink_video_codec *zvc,
            struct pipe_video_buffer *target)
{
   struct zink_resource *luma = (struct zink_resource *)((struct vl_video_buffer *)target)->resources[0];
   struct zink_screen *screen = (struct zink_screen *)zvc->screen;
   struct zink_video_surf_data *surf = calloc(sizeof(*surf), 1);
   if (!surf)
      return NULL;

   surf->codec = zvc;
   surf->resource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
   surf->resource.pNext = NULL;
   surf->resource.codedExtent.width = target->width;
   surf->resource.codedExtent.height = target->height;
   surf->resource.imageViewBinding = VK_NULL_HANDLE;

   struct pipe_surface psurf = {};
   psurf.format = target->buffer_format;
   VkImageViewCreateInfo ivci = create_ivci(screen, luma, &psurf, PIPE_TEXTURE_2D);

   if (VKSCR(CreateImageView)(screen->dev, &ivci, NULL, &surf->resource.imageViewBinding) != VK_SUCCESS) {
      free(surf);
      return NULL;
   }

   return surf;
}

static struct zink_video_surf_data *
get_surf(struct zink_video_codec *zvc, struct pipe_video_buffer *buf)
{
   struct zink_video_surf_data *surf = vl_video_buffer_get_associated_data(buf, &zvc->base);
   if (!surf) {
      surf = create_surf(zvc, buf);
      if (surf)
         vl_video_buffer_set_associated_data(buf, &zvc->base, surf, &zink_destroy_associated_data);
   }
   return surf;
}

static void
end_bitstream(struct zink_video_codec *zvc)
{
   zvc->bs_ptr = NULL;
   pipe_buffer_unmap(zvc->base.context, zvc->bs_xfer);
}

static void
end_coding(struct zink_screen *screen,
           struct zink_video_codec *zvc,
           VkCommandBuffer cmdbuf)
{
   VkVideoEndCodingInfoKHR eci = { 0 };
   eci.sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR;

   VKSCR(CmdEndVideoCodingKHR)(cmdbuf, &eci);

   struct zink_resource *zbs = (struct zink_resource *)zvc->bs[zvc->cur_bs_buf];
   VkBufferMemoryBarrier2KHR bitstream_bmb;
   bitstream_bmb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR;
   bitstream_bmb.pNext = NULL;
   bitstream_bmb.srcStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
   bitstream_bmb.srcAccessMask = VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR;
   bitstream_bmb.dstStageMask = VK_PIPELINE_STAGE_2_NONE_KHR;
   bitstream_bmb.dstAccessMask = VK_ACCESS_2_NONE_KHR;
   bitstream_bmb.srcQueueFamilyIndex = zbs->queue;
   bitstream_bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   bitstream_bmb.buffer = zbs->obj->buffer;
   bitstream_bmb.size = align64(zvc->bs_size, zvc->srcbuf_align);
   bitstream_bmb.offset = 0;
   zbs->queue = VK_QUEUE_FAMILY_IGNORED;

   VkDependencyInfoKHR di = { 0 };
   di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
   di.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
   di.bufferMemoryBarrierCount = 1;
   di.pBufferMemoryBarriers = &bitstream_bmb;
   /* TODO image barriers */
   VKSCR(CmdPipelineBarrier2KHR)(cmdbuf, &di);

   if (VKSCR(EndCommandBuffer)(cmdbuf) != VK_SUCCESS) {
      debug_printf("vkEndCommandBuffer failed\n");
   }

   VkSubmitInfo si = {0};
   si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.waitSemaphoreCount = 0;
   si.pWaitSemaphores = NULL;
   si.signalSemaphoreCount = 0;
   si.pSignalSemaphores = NULL;
   VkPipelineStageFlags videoDecodeSubmitWaitStages = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
   si.pWaitDstStageMask = &videoDecodeSubmitWaitStages;
   si.commandBufferCount = 1;
   si.pCommandBuffers = &cmdbuf;

   VKSCR(QueueSubmit)(screen->queue_video_decode, 1, &si, NULL);
   VKSCR(QueueWaitIdle)(screen->queue_video_decode);
   VKSCR(FreeCommandBuffers)(screen->dev, zvc->cmdpool, 1, &cmdbuf);
   VKSCR(DestroyVideoSessionParametersKHR)(screen->dev,
                                           zvc->params, NULL);

   zvc->cur_bs_buf++;
   zvc->cur_bs_buf %= NUM_BUFFERS;
}

static int
zink_end_frame_h264(struct pipe_video_codec *codec,
                    struct pipe_video_buffer *target,
                    struct pipe_picture_desc *picture)
{
   struct zink_video_codec *zvc = (struct zink_video_codec *)codec;
   struct zink_screen *screen = (struct zink_screen *)zvc->screen;
   struct pipe_h264_picture_desc *h264 = (struct pipe_h264_picture_desc *)picture;

   end_bitstream(zvc);
   if (zvc->error)
      return -1;

   /* sessions are created for progressive content only */
   if (h264->field_pic_flag || !h264->pps->sps->frame_mbs_only_flag)
      return -1;

   int ref_slots[ARRAY_SIZE(h264->ref)];
   int cur_slot = -1;
   for (unsigned j = 0; j < ARRAY_SIZE(h264->ref); j++)
      ref_slots[j] = -1;

   /* drop pictures that are no longer referenced, and find the slots of the ones that are */
   for (unsigned i = 0; i < ARRAY_SIZE(zvc->render_pic_list); i++) {
      if (zvc->render_pic_list[i]) {
         bool found = false;
         for (unsigned j = 0; j < ARRAY_SIZE(h264->ref); j++) {
            if (zvc->render_pic_list[i] == h264->ref[j]) {
               ref_slots[j] = i;
               found = true;
            }
         }
         if (!found)
            zvc->render_pic_list[i] = NULL;
      }
      /* the target can also be a reference (other field) */
      if (zvc->render_pic_list[i] == target)
         cur_slot = i;
   }
   if (cur_slot < 0) {
      for (unsigned i = 0; i < MIN2(ARRAY_SIZE(zvc->render_pic_list), zvc->max_dpb_slots); i++) {
         if (!zvc->render_pic_list[i]) {
            cur_slot = i;
            break;
         }
      }
      if (cur_slot < 0)
         return -1;
   }

   struct zink_video_surf_data *surf = get_surf(zvc, target);
   if (!surf)
      return -1;

   StdVideoDecodeH264ReferenceInfo h264_ref_info[17] = { 0 };
   VkVideoDecodeH264DpbSlotInfoKHR h264_dpb_info[17] = { 0 };
   VkVideoReferenceSlotInfoKHR ref_info[17] = { 0 };
   int num_ref_frames = 0;
   for (unsigned j = 0; j < ARRAY_SIZE(h264->ref); j++) {
      if (ref_slots[j] < 0)
         continue;
      struct zink_video_surf_data *rsurf = vl_video_buffer_get_associated_data(h264->ref[j], &zvc->base);
      if (zvc->coincide_dpb && !rsurf) {
         ref_slots[j] = -1;
         continue;
      }

      unsigned n = num_ref_frames++;
      h264_ref_info[n].PicOrderCnt[0] = h264->field_order_cnt_list[j][0];
      h264_ref_info[n].PicOrderCnt[1] = h264->field_order_cnt_list[j][1];
      h264_ref_info[n].FrameNum = h264->frame_num_list[j];
      /* interlaced content is rejected, so references are frames and the field flags stay clear */
      h264_ref_info[n].flags.used_for_long_term_reference = h264->is_long_term[j];

      h264_dpb_info[n].sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
      h264_dpb_info[n].pStdReferenceInfo = &h264_ref_info[n];

      ref_info[n].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
      ref_info[n].slotIndex = ref_slots[j];
      ref_info[n].pNext = &h264_dpb_info[n];
      ref_info[n].pPictureResource = zvc->coincide_dpb ? &rsurf->resource : &zvc->dpb_resources[ref_slots[j]];
   }

   StdVideoDecodeH264ReferenceInfo h264_setup_info = { 0 };
   VkVideoDecodeH264DpbSlotInfoKHR h264_dpb_setup_info = { 0 };
   VkVideoReferenceSlotInfoKHR setup_info;
   h264_setup_info.PicOrderCnt[0] = h264->field_order_cnt[0];
   h264_setup_info.PicOrderCnt[1] = h264->field_order_cnt[1];
   h264_setup_info.FrameNum = h264->frame_num;

   h264_dpb_setup_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
   h264_dpb_setup_info.pStdReferenceInfo = &h264_setup_info;

   setup_info.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
   setup_info.slotIndex = cur_slot;
   setup_info.pPictureResource = zvc->coincide_dpb ? &surf->resource : &zvc->dpb_resources[cur_slot];
   setup_info.pNext = &h264_dpb_setup_info;
   /* this must be added to the list of 'bound resources' */
   ref_info[num_ref_frames] = setup_info;
   ref_info[num_ref_frames++].slotIndex = -1;

   StdVideoH264SequenceParameterSet sps = { 0 };
   StdVideoH264PictureParameterSet pps = { 0 };
   StdVideoH264ScalingLists pps_scaling_lists = { 0 };

   pps.pScalingLists = &pps_scaling_lists;
   convert_pps_sps(picture, zvc->base.level, &sps, &pps, &pps_scaling_lists);
   pps.num_ref_idx_l0_default_active_minus1 = h264->num_ref_idx_l0_active_minus1;
   pps.num_ref_idx_l1_default_active_minus1 = h264->num_ref_idx_l1_active_minus1;
   VkVideoDecodeH264SessionParametersAddInfoKHR h264add = { 0 };
   h264add.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR;
   h264add.stdSPSCount = 1;
   h264add.pStdSPSs = &sps;
   h264add.stdPPSCount = 1;
   h264add.pStdPPSs = &pps;

   VkVideoDecodeH264SessionParametersCreateInfoKHR h264_create = { 0 };
   h264_create.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
   h264_create.maxStdSPSCount = 1;
   h264_create.maxStdPPSCount = 1;
   h264_create.pParametersAddInfo = &h264add;
   VkVideoSessionParametersCreateInfoKHR pci = { 0 };
   pci.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR;
   pci.pNext = &h264_create;
   pci.videoSession = zvc->session;

   if (VKSCR(CreateVideoSessionParametersKHR)(screen->dev, &pci, NULL, &zvc->params) != VK_SUCCESS)
      return -1;

   /* only take the slot once the decode will be recorded, or later frames would reference an inactive slot */
   zvc->render_pic_list[cur_slot] = target;

   VkCommandBuffer cmdbuf;
   VkCommandBufferAllocateInfo cbai = {0};
   cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cbai.commandPool = zvc->cmdpool;
   cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   cbai.commandBufferCount = 1;

   VKSCR(AllocateCommandBuffers)(screen->dev, &cbai, &cmdbuf);

   VkCommandBufferBeginInfo cbbi = {0};
   cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   VKSCR(BeginCommandBuffer)(cmdbuf, &cbbi);

   VkVideoBeginCodingInfoKHR bci = { 0 };
   bci.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
   bci.videoSession = zvc->session;
   bci.videoSessionParameters = zvc->params;
   bci.referenceSlotCount = num_ref_frames;
   bci.pReferenceSlots = ref_info;

   VKSCR(CmdBeginVideoCodingKHR)(cmdbuf, &bci);

   if (!zvc->reset_sent) {
      VkVideoCodingControlInfoKHR cc = {0};
      cc.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
      cc.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
      VKSCR(CmdControlVideoCodingKHR)(cmdbuf, &cc);
      zvc->reset_sent = true;
   }

   struct zink_resource *zbs = (struct zink_resource *)zvc->bs[zvc->cur_bs_buf];
   VkBufferMemoryBarrier2KHR bitstream_bmb;
   bitstream_bmb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR;
   bitstream_bmb.pNext = NULL;
   bitstream_bmb.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT_KHR;
   bitstream_bmb.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT_KHR;
   bitstream_bmb.dstStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
   bitstream_bmb.dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR;
   bitstream_bmb.srcQueueFamilyIndex = zbs->queue;
   bitstream_bmb.dstQueueFamilyIndex = screen->video_decode_queue;
   bitstream_bmb.buffer = zbs->obj->buffer;
   bitstream_bmb.size = align64(zvc->bs_size, zvc->srcbuf_align);
   bitstream_bmb.offset = 0;
   zbs->queue = screen->video_decode_queue;

   VkDependencyInfoKHR di = { 0 };
   di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
   di.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
   di.bufferMemoryBarrierCount = 1;
   di.pBufferMemoryBarriers = &bitstream_bmb;
   /* TODO image barriers */
   VKSCR(CmdPipelineBarrier2KHR)(cmdbuf, &di);

   StdVideoDecodeH264PictureInfo pi = { 0 };
   pi.frame_num = h264->frame_num;
   pi.PicOrderCnt[0] = h264->field_order_cnt[0];
   pi.PicOrderCnt[1] = h264->field_order_cnt[1];
   pi.flags.field_pic_flag = h264->field_pic_flag;
   pi.flags.bottom_field_flag = h264->bottom_field_flag;
   pi.flags.is_reference = h264->is_reference;

   VkVideoDecodeH264PictureInfoKHR hpi = { 0 };
   hpi.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR;
   hpi.pStdPictureInfo = &pi;
   hpi.sliceCount = h264->slice_count;
   uint32_t slice_data_offsets[ARRAY_SIZE(h264->slice_parameter.slice_data_size)] = {};
   for (unsigned i = 0; i < h264->slice_count; i++) {
      for (unsigned j = i + 1; j < h264->slice_count; j++) {
         slice_data_offsets[j] += h264->slice_parameter.slice_data_size[i] + 3;
      }
   }
   hpi.pSliceOffsets = slice_data_offsets;

   VkVideoDecodeInfoKHR vdi = { 0 };
   vdi.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR;
   vdi.pNext = &hpi;
   vdi.referenceSlotCount = num_ref_frames - 1;
   vdi.pReferenceSlots = ref_info;
   vdi.pSetupReferenceSlot = &setup_info;
   vdi.dstPictureResource = surf->resource;


   vdi.srcBuffer = zbs->obj->buffer;
   vdi.srcBufferOffset = 0;
   vdi.srcBufferRange = align64(zvc->bs_size, zvc->srcbuf_align);
   VKSCR(CmdDecodeVideoKHR)(cmdbuf, &vdi);

   end_coding(screen, zvc, cmdbuf);

   return 0;
}

static void
zink_video_flush(struct pipe_video_codec *codec) {
}

static struct pipe_video_codec *
zink_create_video_codec(struct pipe_context *pctx, const struct pipe_video_codec *templat)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_video_codec *zvc = CALLOC_STRUCT(zink_video_codec);
   if (!zvc) {
      return NULL;
   }

   zvc->base = *templat;
   zvc->base.destroy = zink_destroy_video_codec;
   zvc->base.begin_frame = zink_begin_frame;
   zvc->base.decode_macroblock = zink_decode_macroblock;
   zvc->base.decode_bitstream = zink_decode_bitstream;
   zvc->base.end_frame = zink_end_frame_h264;
   zvc->base.flush = zink_video_flush;
   zvc->base.context = pctx;

   zvc->screen = pctx->screen;

   VkCommandPoolCreateInfo cpci = {0};
   cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cpci.queueFamilyIndex = screen->video_decode_queue;
   if (VKSCR(CreateCommandPool)(screen->dev, &cpci, NULL, &zvc->cmdpool) != VK_SUCCESS)
      return NULL;

   for (unsigned i = 0; i < NUM_BUFFERS; i++) {
      zvc->bs[i] = pipe_buffer_create(&screen->base, ZINK_BIND_VIDEO, PIPE_USAGE_STAGING,
                                      zvc->base.width * zvc->base.height * (512 / (16 * 16)));
   }
   return &zvc->base;
}

static struct pipe_video_buffer *
zink_video_buffer_create(struct pipe_context *pctx, const struct pipe_video_buffer *templ)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct pipe_video_buffer vidbuf = *templ;
   uint64_t *modifiers = NULL;
   int modifiers_count = 0;
   uint64_t mod[3] = { 0 };
   int count = 0;

   vidbuf.bind |= ZINK_BIND_VIDEO;
   if (pctx->screen->query_dmabuf_modifiers)
      pctx->screen->query_dmabuf_modifiers(pctx->screen, templ->buffer_format, 3, mod, NULL, &count);

   if (pctx->screen->resource_create_with_modifiers) {
      modifiers = mod;
      modifiers_count = MIN2(count, ARRAY_SIZE(mod));
   }

   if (screen->video_output_usage & VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR) {
      vidbuf.flags = ZINK_RESOURCE_FLAG_VIDEO_DPB | ZINK_RESOURCE_FLAG_VIDEO_OUTPUT;
   } else {
      vidbuf.flags = ZINK_RESOURCE_FLAG_VIDEO_OUTPUT;
   }

   return vl_video_buffer_create_as_resource(pctx, &vidbuf, modifiers, modifiers_count);
}

static StdVideoH264ProfileIdc
h264_profile_conv(enum pipe_video_profile profile)
{
   switch (profile) {
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      return STD_VIDEO_H264_PROFILE_IDC_MAIN;
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      return STD_VIDEO_H264_PROFILE_IDC_HIGH;
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      return STD_VIDEO_H264_PROFILE_IDC_BASELINE;
   default:
      assert(0);
      return STD_VIDEO_H264_PROFILE_IDC_MAIN;
   }

}

static VkVideoCodecOperationFlagsKHR
convert_decode_op(enum pipe_video_format vid_format)
{
   switch (vid_format) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      return VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
   default:
      assert(0);
      return VK_VIDEO_CODEC_OPERATION_NONE_KHR;
   }
}

bool
zink_video_fill_single_profile(struct zink_screen *screen,
                               enum pipe_video_profile profile,
                               uint32_t luma_depth,
                               struct zink_video_profile *out_prof)
{
   VkVideoComponentBitDepthFlagBitsKHR bit_depth;
   enum pipe_video_format vid_format = u_reduce_video_profile(profile);

   switch (luma_depth) {
   case 10:
      bit_depth = VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
      break;
   default:
      bit_depth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
      break;
   }

   if (vid_format == PIPE_VIDEO_FORMAT_MPEG4_AVC &&
       !screen->info.have_KHR_video_decode_h264)
      return false;

   out_prof->profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;

   out_prof->profile.videoCodecOperation = convert_decode_op(vid_format);
   out_prof->profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
   out_prof->profile.lumaBitDepth = bit_depth;
   out_prof->profile.chromaBitDepth = bit_depth;

   switch (vid_format) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      out_prof->h264.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
      out_prof->h264.stdProfileIdc = h264_profile_conv(profile);
      out_prof->h264.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;
      out_prof->profile.pNext = &out_prof->h264;
      break;
   default:
      return false;
   }
   return true;
}

void
zink_video_fill_profiles(struct zink_screen *screen,
                         struct zink_video_profile_info *profiles,
                         enum pipe_video_profile profile,
                         uint32_t luma_depth)
{
   int profile_count = 0;
   enum pipe_video_format vid_format = u_reduce_video_profile(profile);

   if (screen->info.have_KHR_video_decode_h264 && (vid_format == PIPE_VIDEO_FORMAT_MPEG4_AVC ||
                                                   vid_format == PIPE_VIDEO_FORMAT_UNKNOWN)) {
      if (vid_format == PIPE_VIDEO_FORMAT_UNKNOWN)
         profile = PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN;
      if (zink_video_fill_single_profile(screen, profile, luma_depth, &profiles->h264)) {
         profiles->profiles[profile_count] = profiles->h264.profile;
         profile_count++;
      }
   }

   profiles->list.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
   profiles->list.profileCount = profile_count;
   profiles->list.pProfiles = profiles->profiles;
}

void
zink_video_init(struct zink_context *ctx)
{
   ctx->base.create_video_codec = zink_create_video_codec;
   ctx->base.create_video_buffer = zink_video_buffer_create;
}

bool
zink_video_fill_caps(struct zink_screen *screen,
                     enum pipe_video_profile profile,
                     enum pipe_video_entrypoint entrypoint,
                     bool bit_depth_10,
                     struct zink_video_caps_info *caps_info)
{
   struct zink_video_profile vk_profile = { 0 };
   if (!zink_video_fill_single_profile(screen, profile, bit_depth_10 ? 10 : 8, &vk_profile))
      return false;

   caps_info->caps.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
   caps_info->caps.pNext = &caps_info->dec_caps;
   caps_info->dec_caps.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;

   caps_info->dec_caps.pNext = &caps_info->h264_dec_caps;
   caps_info->h264_dec_caps.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR;

   return VKSCR(GetPhysicalDeviceVideoCapabilitiesKHR)(screen->pdev, &vk_profile.profile, &caps_info->caps) == VK_SUCCESS;
}

VkResult
zink_fill_video_format_props(struct zink_screen *screen,
                             VkImageUsageFlags usage,
                             enum pipe_video_profile profile,
                             uint32_t bit_depth,
                             struct zink_video_format_prop *props)
{
   struct zink_video_profile_info profiles = {0};
   zink_video_fill_profiles(screen, &profiles, profile, bit_depth);

   VkPhysicalDeviceVideoFormatInfoKHR video_format_info = {};
   video_format_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR;
   video_format_info.pNext = &profiles.list;
   video_format_info.imageUsage = usage;

   props->pVideoFormatProperties = NULL;
   props->videoFormatPropertyCount = 0;
   uint32_t count = 0;
   VkResult ret = VKSCR(GetPhysicalDeviceVideoFormatPropertiesKHR)(screen->pdev, &video_format_info,
                                                                   &count, NULL);
   if (ret != VK_SUCCESS)
      return ret;
   VkVideoFormatPropertiesKHR *fprops = calloc(count, sizeof(VkVideoFormatPropertiesKHR));
   if (!fprops)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   for (unsigned i = 0; i < count; i++)
      fprops[i].sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
   ret = VKSCR(GetPhysicalDeviceVideoFormatPropertiesKHR)(screen->pdev, &video_format_info,
                                                          &count, fprops);
   if (ret != VK_SUCCESS) {
      free(fprops);
      return ret;
   }
   props->pVideoFormatProperties = fprops;
   props->videoFormatPropertyCount = count;
   return VK_SUCCESS;
}

void
zink_video_init_format_props(struct zink_screen *screen)
{
   struct zink_video_caps_info caps_info = { 0 };
   bool coincide = zink_video_fill_caps(screen, PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH,
                                        PIPE_VIDEO_ENTRYPOINT_BITSTREAM, false, &caps_info) &&
                   (caps_info.dec_caps.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR);
   screen->video_output_usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
   if (coincide)
      screen->video_output_usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;

   for (unsigned i = 0; i < ARRAY_SIZE(screen->video_format_prop_dpb); i++) {
      uint32_t bit_depth = i ? 10 : 8;
      /* querying format properties for an unsupported profile is invalid */
      if (!zink_video_fill_caps(screen, PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH,
                                PIPE_VIDEO_ENTRYPOINT_BITSTREAM, bit_depth == 10, &caps_info))
         continue;
      zink_fill_video_format_props(screen, VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR,
                                   PIPE_VIDEO_PROFILE_UNKNOWN, bit_depth,
                                   &screen->video_format_prop_dpb[i]);
      zink_fill_video_format_props(screen, screen->video_output_usage,
                                   PIPE_VIDEO_PROFILE_UNKNOWN, bit_depth,
                                   &screen->video_format_prop_output[i]);
   }
}
