/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ZINK_CONTEXT_H
#define ZINK_CONTEXT_H

#define ZINK_SHADER_COUNT (PIPE_SHADER_TYPES - 1)
#define ZINK_NUM_GFX_BATCHES 4
#define ZINK_COMPUTE_BATCH_ID ZINK_NUM_GFX_BATCHES
#define ZINK_COMPUTE_BATCH_COUNT 1
#define ZINK_NUM_BATCHES (ZINK_NUM_GFX_BATCHES + 1)

#define ZINK_DEFAULT_MAX_DESCS 5000

#include "zink_clear.h"
#include "zink_pipeline.h"
#include "zink_batch.h"
#include "zink_compiler.h"
#include "zink_descriptors.h"

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_rect.h"

#include "util/slab.h"
#include "util/list.h"

#include <vulkan/vulkan.h>

struct blitter_context;
struct primconvert_context;
struct list_head;

struct zink_blend_state;
struct zink_depth_stencil_alpha_state;
struct zink_gfx_program;
struct zink_rasterizer_state;
struct zink_resource;
struct zink_surface;
struct zink_vertex_elements_state;

enum zink_blit_flags {
   ZINK_BLIT_NORMAL = 1 << 0,
   ZINK_BLIT_SAVE_FS = 1 << 1,
   ZINK_BLIT_SAVE_FB = 1 << 2,
   ZINK_BLIT_SAVE_TEXTURES = 1 << 3,
};


struct zink_sampler_state {
   VkSampler sampler;
   uint32_t hash;
   struct zink_descriptor_refs desc_set_refs;
   bool custom_border_color;
};

struct zink_sampler_view {
   struct pipe_sampler_view base;
   struct zink_descriptor_refs desc_set_refs;
   union {
      VkImageView image_view;
      VkBufferView buffer_view;
   };
   uint32_t hash;
};

struct zink_image_view {
   struct pipe_image_view base;
   struct zink_descriptor_refs desc_set_refs;
   union {
      struct zink_surface *surface;
      VkBufferView buffer_view;
   };
};

static inline struct zink_sampler_view *
zink_sampler_view(struct pipe_sampler_view *pview)
{
   return (struct zink_sampler_view *)pview;
}

struct zink_so_target {
   struct pipe_stream_output_target base;
   struct pipe_resource *counter_buffer;
   VkDeviceSize counter_buffer_offset;
   uint32_t stride;
   bool counter_buffer_valid;
};

static inline struct zink_so_target *
zink_so_target(struct pipe_stream_output_target *so_target)
{
   return (struct zink_so_target *)so_target;
}

struct zink_viewport_state {
   struct pipe_viewport_state viewport_states[PIPE_MAX_VIEWPORTS];
   struct pipe_scissor_state scissor_states[PIPE_MAX_VIEWPORTS];
   uint8_t num_viewports;
};


/* hashes of all the named types in a given state */
struct zink_descriptor_state {
   bool valid[ZINK_DESCRIPTOR_TYPES];
   uint32_t state[ZINK_DESCRIPTOR_TYPES];
};

struct zink_context {
   struct pipe_context base;
   struct slab_child_pool transfer_pool;
   struct blitter_context *blitter;

   struct pipe_device_reset_callback reset;

   struct zink_batch batches[ZINK_NUM_GFX_BATCHES];
   bool is_device_lost;
   unsigned curr_batch;

   VkQueue queue;

   struct zink_batch compute_batch;

   struct pipe_constant_buffer ubos[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
   struct pipe_shader_buffer ssbos[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_BUFFERS];
   uint32_t writable_ssbos[PIPE_SHADER_TYPES];
   struct zink_image_view image_views[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];

   struct pipe_framebuffer_state fb_state;

   struct zink_vertex_elements_state *element_state;
   struct zink_rasterizer_state *rast_state;
   struct zink_depth_stencil_alpha_state *dsa_state;

   struct zink_shader *gfx_stages[ZINK_SHADER_COUNT];
   struct zink_gfx_pipeline_state gfx_pipeline_state;
   enum pipe_prim_type gfx_prim_mode;
   struct hash_table *program_cache;
   struct zink_gfx_program *curr_program;

   struct zink_descriptor_state gfx_descriptor_states[ZINK_SHADER_COUNT]; // keep incremental hashes here
   struct zink_descriptor_state descriptor_states[2]; // gfx, compute
   struct hash_table *descriptor_pools[ZINK_DESCRIPTOR_TYPES];

   struct zink_shader *compute_stage;
   struct zink_compute_pipeline_state compute_pipeline_state;
   struct hash_table *compute_program_cache;
   struct zink_compute_program *curr_compute;

   unsigned dirty_shader_stages : 6; /* mask of changed shader stages */
   bool last_vertex_stage_dirty;

   struct hash_table *render_pass_cache;

   struct primconvert_context *primconvert;

   struct zink_framebuffer *framebuffer;
   struct zink_framebuffer_clear fb_clears[PIPE_MAX_COLOR_BUFS + 1];

   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];

   void *sampler_states[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   VkSampler samplers[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   unsigned num_samplers[PIPE_SHADER_TYPES];
   struct pipe_sampler_view *sampler_views[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   unsigned num_sampler_views[PIPE_SHADER_TYPES];

   struct zink_viewport_state vp_state;

   float line_width;
   float blend_constants[4];

   bool drawid_broken;

   struct pipe_stencil_ref stencil_ref;

   union {
      struct {
         float default_inner_level[2];
         float default_outer_level[4];
      };
      float tess_levels[6];
   };

   struct list_head suspended_queries;
   struct list_head primitives_generated_queries;
   bool queries_disabled, render_condition_active;

   struct pipe_resource *dummy_vertex_buffer;
   struct pipe_resource *dummy_xfb_buffer;
   struct pipe_resource *null_buffers[5]; /* used to create zink_framebuffer->null_surface, one buffer per samplecount */

   uint32_t num_so_targets;
   struct pipe_stream_output_target *so_targets[PIPE_MAX_SO_OUTPUTS];
   bool dirty_so_targets;
   bool xfb_barrier;
};

static inline struct zink_context *
zink_context(struct pipe_context *context)
{
   return (struct zink_context *)context;
}

static inline struct zink_batch *
zink_curr_batch(struct zink_context *ctx)
{
   assert(ctx->curr_batch < ARRAY_SIZE(ctx->batches));
   return ctx->batches + ctx->curr_batch;
}

static inline struct zink_batch *
zink_prev_batch(struct zink_context *ctx)
{
   unsigned curr_batch = ctx->curr_batch;
   if (!curr_batch)
      curr_batch = ZINK_NUM_GFX_BATCHES - 1;
   else
      curr_batch--;
   assert(curr_batch < ARRAY_SIZE(ctx->batches));
   return ctx->batches + curr_batch;
}

struct zink_batch *
zink_batch_rp(struct zink_context *ctx);

struct zink_batch *
zink_batch_no_rp(struct zink_context *ctx);

void
zink_fence_wait(struct pipe_context *ctx);

void
zink_wait_on_batch(struct zink_context *ctx, int batch_id);

void
zink_flush_compute(struct zink_context *ctx);

struct zink_batch *
zink_flush_batch(struct zink_context *ctx, struct zink_batch *batch);

bool
zink_resource_access_is_write(VkAccessFlags flags);

bool
zink_resource_buffer_needs_barrier(struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline);

void
zink_resource_buffer_barrier(struct zink_context *ctx, struct zink_batch *batch, struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline);

bool
zink_resource_image_needs_barrier(struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);
void
zink_resource_image_barrier(struct zink_context *ctx, struct zink_batch *batch, struct zink_resource *res,
                      VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);

bool
zink_resource_needs_barrier(struct zink_resource *res, VkImageLayout layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);
void
zink_resource_barrier(struct zink_context *ctx, struct zink_batch *batch, struct zink_resource *res, VkImageLayout layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);

 void
 zink_begin_render_pass(struct zink_context *ctx,
                        struct zink_batch *batch);

VkPipelineStageFlags
zink_pipeline_flags_from_stage(VkShaderStageFlagBits stage);

VkShaderStageFlagBits
zink_shader_stage(enum pipe_shader_type type);

struct pipe_context *
zink_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags);

void
zink_context_query_init(struct pipe_context *ctx);

void
zink_blit_begin(struct zink_context *ctx, enum zink_blit_flags flags);

void
zink_blit(struct pipe_context *pctx,
          const struct pipe_blit_info *info);

bool
zink_blit_region_fills(struct u_rect region, unsigned width, unsigned height);

bool
zink_blit_region_covers(struct u_rect region, struct u_rect covers);

static inline struct u_rect
zink_rect_from_box(const struct pipe_box *box)
{
   return (struct u_rect){box->x, box->x + box->width, box->y, box->y + box->height};
}

void
zink_draw_vbo(struct pipe_context *pctx,
              const struct pipe_draw_info *dinfo,
              const struct pipe_draw_indirect_info *indirect,
              const struct pipe_draw_start_count *draws,
              unsigned num_draws);

void
zink_launch_grid(struct pipe_context *pctx, const struct pipe_grid_info *info);

void
zink_copy_buffer(struct zink_context *ctx, struct zink_batch *batch, struct zink_resource *dst, struct zink_resource *src,
                 unsigned dst_offset, unsigned src_offset, unsigned size);

void
zink_context_update_descriptor_states(struct zink_context *ctx, bool is_compute);

#endif
