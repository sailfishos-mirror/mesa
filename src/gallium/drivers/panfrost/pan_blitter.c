/*
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/format/u_format.h"
#include "pan_blitter.h"
#include "pan_context.h"
#include "pan_resource.h"
#include "pan_trace.h"
#include "pan_util.h"

enum panfrost_blitter_op /* bitmask */
{
   PAN_SAVE_TEXTURES = 1,
   PAN_SAVE_FRAMEBUFFER = 2,
   PAN_SAVE_FRAGMENT_STATE = 4,
   PAN_SAVE_FRAGMENT_CONSTANT = 8,
   PAN_DISABLE_RENDER_COND = 16,
};

enum {
   PAN_RENDER_BLIT =
      PAN_SAVE_TEXTURES | PAN_SAVE_FRAMEBUFFER | PAN_SAVE_FRAGMENT_STATE,
   PAN_RENDER_BLIT_COND = PAN_SAVE_TEXTURES | PAN_SAVE_FRAMEBUFFER |
                          PAN_SAVE_FRAGMENT_STATE | PAN_DISABLE_RENDER_COND,
   PAN_RENDER_BASE = PAN_SAVE_FRAMEBUFFER | PAN_SAVE_FRAGMENT_STATE,
   PAN_RENDER_COND =
      PAN_SAVE_FRAMEBUFFER | PAN_SAVE_FRAGMENT_STATE | PAN_DISABLE_RENDER_COND,
   PAN_RENDER_CLEAR = PAN_SAVE_FRAGMENT_STATE | PAN_SAVE_FRAGMENT_CONSTANT,
};

static void
panfrost_blitter_draw_rectangle(struct blitter_context *blitter,
                                void *vertex_elements_cso,
                                blitter_get_vs_func get_vs,
                                int x1, int y1, int x2, int y2,
                                float depth, unsigned num_instances,
                                enum blitter_attrib_type type,
                                const struct blitter_attrib *attrib)
{
   assert(num_instances);

   struct pipe_context *ctx = blitter->pipe;
   struct panfrost_context *pctx = pan_context(ctx);
   struct panfrost_screen *scr = pan_screen(ctx->screen);

   if (scr->dev.arch <= 8 || depth != 0.0f || num_instances > 1)
      goto fallback;

   /* Map viewport to the dest rect of the framebuffer. The tiler will then be
    * configured to use it as scissor box in order to clip fullscreen
    * fragments lying outside.
    *
    * Note that: tx = x1 + ((x2 - x1) / 2) = (x2 + x1) / 2
    *            ty = y1 + ((y2 - y1) / 2) = (y2 + y1) / 2
    */
   const struct pipe_viewport_state viewport_state = {
      .scale     = { 0.5f * (x2 - x1), 0.5f * (y2 - y1), 1.0f },
      .translate = { 0.5f * (x2 + x1), 0.5f * (y2 + y1), 0.0f },
      .swizzle_x = PIPE_VIEWPORT_SWIZZLE_POSITIVE_X,
      .swizzle_y = PIPE_VIEWPORT_SWIZZLE_POSITIVE_Y,
      .swizzle_z = PIPE_VIEWPORT_SWIZZLE_POSITIVE_Z,
      .swizzle_w = PIPE_VIEWPORT_SWIZZLE_POSITIVE_W
   };
   ctx->set_viewport_states(ctx, 0, 1, &viewport_state);

   /* Map texture coordinates to the fullscreen framebuffer. */
   struct blitter_attrib fs_attrib;
   if (type == UTIL_BLITTER_ATTRIB_TEXCOORD_XY ||
       type == UTIL_BLITTER_ATTRIB_TEXCOORD_XYZW) {
      float dfdx = (attrib->texcoord.x2 - attrib->texcoord.x1) / (x2 - x1);
      float dfdy = (attrib->texcoord.y2 - attrib->texcoord.y1) / (y2 - y1);
      float w = pctx->pipe_framebuffer.width;
      float h = pctx->pipe_framebuffer.height;
      fs_attrib = *attrib;
      fs_attrib.texcoord.x1 -= dfdx * x1;
      fs_attrib.texcoord.y1 -= dfdy * y1;
      fs_attrib.texcoord.x2 += dfdx * (w - x2);
      fs_attrib.texcoord.y2 += dfdy * (h - y2);
   };

   scr->vtbl.draw_fullscreen(pan_context(ctx), get_vs(blitter), type,
                             &fs_attrib);
   return;

 fallback:
   /* Fallback to draw_vbo. */
   util_blitter_draw_rectangle(blitter, vertex_elements_cso, get_vs, x1, y1,
                               x2, y2, depth, num_instances, type, attrib);
}

struct blitter_context *
panfrost_blitter_create(struct pipe_context *pipe)
{
   struct blitter_context *blitter;

   blitter = util_blitter_create(pipe);
   blitter->draw_rectangle = panfrost_blitter_draw_rectangle;

   return blitter;
}

static void
panfrost_blitter_save(struct panfrost_context *ctx,
                      const enum panfrost_blitter_op blitter_op)
{
   struct blitter_context *blitter = ctx->blitter;

   util_blitter_save_vertex_buffers(blitter, ctx->vertex_buffers,
                                    util_last_bit(ctx->vb_mask));
   util_blitter_save_vertex_elements(blitter, ctx->vertex);
   util_blitter_save_vertex_shader(blitter,
                                   ctx->uncompiled[MESA_SHADER_VERTEX]);
   util_blitter_save_rasterizer(blitter, ctx->rasterizer);
   util_blitter_save_viewport(blitter, &ctx->pipe_viewport);
   util_blitter_save_so_targets(blitter, 0, NULL, 0);

   if (blitter_op & PAN_SAVE_FRAGMENT_STATE) {
      if (blitter_op & PAN_SAVE_FRAGMENT_CONSTANT)
         util_blitter_save_fragment_constant_buffer_slot(
            blitter, ctx->constant_buffer[MESA_SHADER_FRAGMENT].cb);

      util_blitter_save_blend(blitter, ctx->blend);
      util_blitter_save_depth_stencil_alpha(blitter, ctx->depth_stencil);
      util_blitter_save_stencil_ref(blitter, &ctx->stencil_ref);
      util_blitter_save_fragment_shader(blitter,
                                        ctx->uncompiled[MESA_SHADER_FRAGMENT]);
      util_blitter_save_sample_mask(blitter, ctx->sample_mask,
                                    ctx->min_samples);
      util_blitter_save_scissor(blitter, &ctx->scissor);
   }

   if (blitter_op & PAN_SAVE_FRAMEBUFFER)
      util_blitter_save_framebuffer(blitter, &ctx->pipe_framebuffer);

   if (blitter_op & PAN_SAVE_TEXTURES) {
      util_blitter_save_fragment_sampler_states(
         blitter, ctx->sampler_count[MESA_SHADER_FRAGMENT],
         (void **)(&ctx->samplers[MESA_SHADER_FRAGMENT]));
      util_blitter_save_fragment_sampler_views(
         blitter, ctx->sampler_view_count[MESA_SHADER_FRAGMENT],
         (struct pipe_sampler_view **)&ctx->sampler_views[MESA_SHADER_FRAGMENT]);
   }

   if (!(blitter_op & PAN_DISABLE_RENDER_COND)) {
      util_blitter_save_render_condition(blitter,
                                         (struct pipe_query *)ctx->cond_query,
                                         ctx->cond_cond, ctx->cond_mode);
   }
}

void
panfrost_blitter_blit_no_afbc_legalization(struct pipe_context *pipe,
                                           const struct pipe_blit_info *info)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);

   panfrost_blitter_save(ctx, info->render_condition_enable
                                 ? PAN_RENDER_BLIT_COND
                                 : PAN_RENDER_BLIT);
   util_blitter_blit(ctx->blitter, info, NULL);
}

void
panfrost_blitter_blit(struct pipe_context *pipe,
                      const struct pipe_blit_info *info)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);

   if (info->render_condition_enable && !panfrost_render_condition_check(ctx))
      return;

   if (!util_blitter_is_blit_supported(ctx->blitter, info))
      UNREACHABLE("Unsupported blit\n");

   pan_legalize_format(ctx, pan_resource(info->src.resource),
                       util_format_linear(info->src.format), false, false);
   pan_legalize_format(ctx, pan_resource(info->dst.resource),
                       util_format_linear(info->dst.format), true, false);
   panfrost_flush_all_batches(ctx, "Blit");
   panfrost_blitter_blit_no_afbc_legalization(pipe, info);
   panfrost_flush_all_batches(ctx, "Blit");
}

void
panfrost_blitter_clear(struct pipe_context *pipe, unsigned buffers,
                       uint32_t color_clear_mask, uint8_t stencil_clear_mask,
                       const struct pipe_scissor_state *scissor_state,
                       const union pipe_color_union *color, double depth,
                       unsigned stencil)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   if (!panfrost_render_condition_check(pan_context(pipe)))
      return;

   /* Only get batch after checking the render condition, since the check can
    * cause the batch to be flushed.
    */
   struct panfrost_context *ctx = pan_context(pipe);
   struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
   if (!batch)
      return;

   /* At the start of the batch, we can clear for free */
   if (batch->draw_count == 0) {
      panfrost_batch_clear(batch, buffers, color, depth, stencil);
      return;
   }

   /* Once there is content, clear with a fullscreen quad */
   panfrost_blitter_save(ctx, PAN_RENDER_CLEAR);

   /* Framebuffer legalization is done at batch initialization. */
   perf_debug(ctx, "Clearing with quad");
   util_blitter_clear(
      ctx->blitter, ctx->pipe_framebuffer.width, ctx->pipe_framebuffer.height,
      util_framebuffer_get_num_layers(&ctx->pipe_framebuffer), buffers, color,
      depth, stencil,
      util_framebuffer_get_num_samples(&ctx->pipe_framebuffer) > 1);
}

void
panfrost_blitter_clear_depth_stencil(struct pipe_context *pipe,
                                     struct pipe_surface *dst,
                                     unsigned clear_flags, double depth,
                                     unsigned stencil, unsigned dstx,
                                     unsigned dsty, unsigned width,
                                     unsigned height,
                                     bool render_condition_enabled)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);

   if (render_condition_enabled && !panfrost_render_condition_check(ctx))
      return;

   pan_legalize_format(ctx, pan_resource(dst->texture),
                       util_format_linear(dst->format), true, false);
   panfrost_blitter_save(
      ctx, render_condition_enabled ? PAN_RENDER_COND : PAN_RENDER_BASE);
   util_blitter_clear_depth_stencil(ctx->blitter, dst, clear_flags, depth,
                                    stencil, dstx, dsty, width, height);
}

void
panfrost_blitter_clear_render_target(struct pipe_context *pipe,
                                     struct pipe_surface *dst,
                                     const union pipe_color_union *color,
                                     unsigned dstx, unsigned dsty,
                                     unsigned width, unsigned height,
                                     bool render_condition_enabled)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);

   if (render_condition_enabled && !panfrost_render_condition_check(ctx))
      return;

   pan_legalize_format(ctx, pan_resource(dst->texture),
                       util_format_linear(dst->format), true, false);
   panfrost_blitter_save(
      ctx, (render_condition_enabled ? PAN_RENDER_COND : PAN_RENDER_BASE) | PAN_SAVE_FRAGMENT_CONSTANT);
   util_blitter_clear_render_target(ctx->blitter, dst, color, dstx, dsty,
                                    width, height);
}
