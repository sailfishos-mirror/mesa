/*
 * Copyright © 2026 Valve Corporation.
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_cmd_buffer.h"
#include "tu_tile_config.h"

static void
tu_calc_frag_area(struct tu_cmd_buffer *cmd,
                  struct tu_tile_config *tile,
                  const struct tu_image_view *fdm,
                  const VkOffset2D *fdm_offsets)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   const uint32_t x1 = tiling->tile0.width * tile->pos.x;
   const uint32_t y1 = tiling->tile0.height * tile->pos.y;
   const uint32_t x2 = MIN2(x1 + tiling->tile0.width, MAX_VIEWPORT_SIZE);
   const uint32_t y2 = MIN2(y1 + tiling->tile0.height, MAX_VIEWPORT_SIZE);

   unsigned views = tu_fdm_num_layers(cmd);
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   struct tu_frag_area raw_areas[views];
   if (fdm) {
      for (unsigned i = 0; i < views; i++) {
         VkOffset2D sample_pos = { 0, 0 };

         /* Offsets less than a tile size are accomplished by sliding the
          * tiles.  However once we shift a whole tile size then we reset the
          * tiles back to where they were at the beginning and we need to
          * adjust where each bin is sampling from:
          *
          * x offset = 0:
          *
          * ------------------------------------
          * |   *   |   *   |   *   | (unused) |
          * ------------------------------------
          *
          * x offset = 4:
          *
          * -------------------------
          * | * |   *   |   *   | * |
          * -------------------------
          *
          * x offset = 8:
          *
          * ------------------------------------
          * |   *   |   *   |   *   | (unused) |
          * ------------------------------------
          *
          * As the user's offset increases we slide the tiles to the right,
          * until we reach the whole tile size and reset the tile positions.
          * tu_bin_offset() returns an amount to shift to the left, negating
          * the offset.
          *
          * If we were forced to use a shared viewport, then we must not shift
          * over the tiles and instead must only shift when sampling because
          * we cannot shift the tiles differently per view. This disables
          * smooth transitions of the fragment density map and effectively
          * negates the extension.
          *
          * Note that we cannot clamp x2/y2 to the framebuffer size, as we
          * normally would do, because then tiles along the edge would
          * incorrectly nudge the sample_pos towards the center of the
          * framebuffer. If we shift one complete tile over towards the
          * center and reset the tiles as above, the sample_pos would
          * then shift back towards the edge and we could get a "pop" from
          * suddenly changing density due to the slight shift.
          */
         if (fdm_offsets) {
            VkOffset2D offset = fdm_offsets[i];
            if (!cmd->state.rp.shared_viewport) {
               VkOffset2D bin_offset = tu_bin_offset(fdm_offsets[i], tiling);
               offset.x += bin_offset.x;
               offset.y += bin_offset.y;
            }
            sample_pos.x = (x1 + x2) / 2 - offset.x;
            sample_pos.y = (y1 + y2) / 2 - offset.y;
         } else {
            sample_pos.x = (x1 + MIN2(x2, fb->width)) / 2;
            sample_pos.y = (y1 + MIN2(y2, fb->height)) / 2;
         }

         tu_fragment_density_map_sample(fdm,
                                        sample_pos.x,
                                        sample_pos.y,
                                        fb->width, fb->height, i,
                                        &raw_areas[i]);
      }
   } else {
      for (unsigned i = 0; i < views; i++)
         raw_areas[i].width = raw_areas[i].height = 1.0f;
   }

   for (unsigned i = 0; i < views; i++) {
      float floor_x, floor_y;
      float area = raw_areas[i].width * raw_areas[i].height;
      float frac_x = modff(raw_areas[i].width, &floor_x);
      float frac_y = modff(raw_areas[i].height, &floor_y);

      /* The Vulkan spec says that a density of 0 results in an undefined
       * fragment area. However the blob driver skips rendering tiles with 0
       * density, and apps rely on that behavior. Replicate that here.
       */
      if (!isfinite(area)) {
         tile->frag_areas[i].width = UINT32_MAX;
         tile->frag_areas[i].height = UINT32_MAX;
         tile->visible_views &= ~(1u << i);
         continue;
      }

      /* The spec allows rounding up one of the axes as long as the total
       * area is less than or equal to the original area. Take advantage of
       * this to try rounding up the number with the largest fraction.
       */
      if ((frac_x > frac_y ? (floor_x + 1.f) * floor_y :
                              floor_x * (floor_y + 1.f)) <= area) {
         if (frac_x > frac_y)
            floor_x += 1.f;
         else
            floor_y += 1.f;
      }
      uint32_t width = floor_x;
      uint32_t height = floor_y;

      /* Areas that aren't a power of two, especially large areas, can create
       * in floating-point rounding errors when dividing by the area in the
       * viewport that result in under-rendering. Round down to a power of two
       * to make sure all operations are exact.
       */
      width = 1u << util_logbase2(width);
      height = 1u << util_logbase2(height);

      /* When FDM offset is enabled, the fragment area has to divide the
       * offset to make sure that we don't have tiles with partial fragments.
       * It would be bad to have the fragment area change as a function of the
       * offset, because we'd get "popping" as the resolution changes with the
       * offset, so just make sure it divides the offset granularity. This
       * should mean it always divides the offset for any possible offset.
       */
      if (fdm_offsets) {
         width = MIN2(width, TU_FDM_OFFSET_GRANULARITY);
         height = MIN2(height, TU_FDM_OFFSET_GRANULARITY);
      }

      /* HW viewport scaling supports a maximum fragment width/height of 4.
       */
      if (views <= MAX_HW_SCALED_VIEWS) {
         width = MIN2(width, 4);
         height = MIN2(height, 4);
      }

      /* Make sure that the width/height divides the tile width/height so
       * we don't have to do extra awkward clamping of the edges of each
       * bin when resolving. It also has to divide the fdm offset, if any.
       * Note that because the tile width is rounded to a multiple of 32 any
       * power of two 32 or less will work, and if there is an offset then it
       * must be a multiple of 4 so 2 or 4 will definitely work.
       *
       * TODO: Try to take advantage of the total area allowance here, too.
       */
      while (tiling->tile0.width % width != 0)
         width /= 2;
      while (tiling->tile0.height % height != 0)
         height /= 2;

      tile->frag_areas[i].width = width;
      tile->frag_areas[i].height = height;
   }

   /* If at any point we were forced to use the same scaling for all
    * viewports, we need to make sure that any users *not* using shared
    * scaling, including loads/stores, also consistently share the scaling. 
    */
   if (cmd->state.rp.shared_viewport) {
      VkExtent2D frag_area = { UINT32_MAX, UINT32_MAX };
      for (unsigned i = 0; i < views; i++) {
         frag_area.width = MIN2(frag_area.width, tile->frag_areas[i].width);
         frag_area.height = MIN2(frag_area.height, tile->frag_areas[i].height);
      }

      for (unsigned i = 0; i < views; i++)
         tile->frag_areas[i] = frag_area;
   }
}

static bool
rects_intersect(VkRect2D a, VkRect2D b)
{
   return a.offset.x < b.offset.x + (int32_t)b.extent.width &&
          b.offset.x < a.offset.x + (int32_t)a.extent.width &&
          a.offset.y < b.offset.y + (int32_t)b.extent.height &&
          b.offset.y < a.offset.y + (int32_t)a.extent.height;
}

/* Use the render area(s) to figure out which views of the bin are visible.
 */
void
tu_calc_bin_visibility(struct tu_cmd_buffer *cmd,
                       struct tu_tile_config *tile,
                       const VkOffset2D *offsets)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   uint32_t views = tu_fdm_num_layers(cmd);
   VkRect2D bin = {
      {
         tile->pos.x * tiling->tile0.width,
         tile->pos.y * tiling->tile0.height
      },
      tiling->tile0
   };

   tile->visible_views = 0;
   for (unsigned i = 0; i < views; i++) {
      VkRect2D offsetted_bin = bin;
      if (offsets && !cmd->state.rp.shared_viewport) {
         VkOffset2D bin_offset = tu_bin_offset(offsets[i], tiling);
         offsetted_bin.offset.x -= bin_offset.x;
         offsetted_bin.offset.y -= bin_offset.y;
      }

      if (rects_intersect(offsetted_bin,
                          cmd->state.per_layer_render_area ?
                          cmd->state.render_areas[i] :
                          cmd->state.render_areas[0])) {
         tile->visible_views |= (1u << i);
      }
   }
}

static bool
try_merge_tiles(struct tu_tile_config *dst, struct tu_tile_config *src,
                unsigned views, bool has_abs_bin_mask, bool shared_viewport)
{
   uint32_t slot_mask = dst->slot_mask | src->slot_mask;
   uint32_t visible_views = dst->visible_views | src->visible_views;

   /* The fragment areas must be the same for views where both bins are
    * visible.
    */
   for (unsigned i = 0; i < views; i++) {
      if ((dst->visible_views & src->visible_views & (1u << i)) &&
          (dst->frag_areas[i].width != src->frag_areas[i].width ||
           dst->frag_areas[i].height != src->frag_areas[i].height))
         return false;
   }

   /* The tiles must be vertically or horizontally adjacent and have the
    * compatible width/height.
    */
   if (dst->pos.x == src->pos.x) {
      if (dst->sysmem_extent.height != src->sysmem_extent.height)
         return false;
   } else if (dst->pos.y == src->pos.y) {
      if (dst->sysmem_extent.width != src->sysmem_extent.width)
         return false;
   } else {
      return false;
   }

   if (dst->gmem_extent.width != src->gmem_extent.width ||
       dst->gmem_extent.height != src->gmem_extent.height)
      return false;

   if (!has_abs_bin_mask) {
      /* The mask of the combined tile has to fit in 16 bits */
      uint32_t hw_mask = slot_mask >> (ffs(slot_mask) - 1);
      if ((hw_mask & 0xffff) != hw_mask)
         return false;
   }

   /* Note, this assumes that dst is below or to the right of src, which is
    * how we call this function below.
    */
   VkExtent2D extent = {
      dst->sysmem_extent.width + (dst->pos.x - src->pos.x),
      dst->sysmem_extent.height + (dst->pos.y - src->pos.y),
   };

   assert(dst->sysmem_extent.height > 0);

   /* If only the first view is visible in both tiles, we can reuse the GMEM
    * space meant for the rest of the views to multiply the height of the
    * tile. We can't do this if we can't override the scissor for different
    * views though.
    */
   unsigned height_multiplier = 1;
   if (visible_views == 1 && views > 1 && dst->gmem_extent.height == 1 &&
       !shared_viewport)
      height_multiplier = views;
   else
      height_multiplier = dst->gmem_extent.height;

   /* The combined fragment areas must not be smaller than the combined bin
    * extent, so that the combined bin is not larger than the original
    * unscaled bin.
    */
   for (unsigned i = 0; i < views; i++) {
      if ((dst->visible_views & (1u << i)) &&
          (dst->frag_areas[i].width < extent.width ||
           dst->frag_areas[i].height * height_multiplier < extent.height))
         return false;
      if ((src->visible_views & (1u << i)) &&
          (src->frag_areas[i].width < extent.width ||
           src->frag_areas[i].height * height_multiplier < extent.height))
         return false;
   }

   /* Ok, let's combine them. dst is below or to the right of src, so it takes
    * src's position.
    */
   for (unsigned i = 0; i < views; i++) {
      if (src->visible_views & ~dst->visible_views & (1u << i))
         dst->frag_areas[i] = src->frag_areas[i];
      if (((src->visible_views | dst->visible_views) & (1u << i)) &&
          dst->frag_areas[i].height < extent.height)
         dst->gmem_extent.height = height_multiplier;
   }
   dst->sysmem_extent = extent;
   dst->visible_views = visible_views;
   dst->pos = src->pos;
   dst->slot_mask = slot_mask;

   src->merged_tile = dst;

   return true;
}

static void
tu_merge_tiles(struct tu_cmd_buffer *cmd, const struct tu_vsc_config *vsc,
               struct tu_tile_config *tiles,
               uint32_t tx1, uint32_t ty1, uint32_t tx2, uint32_t ty2)
{
   bool has_abs_mask =
      cmd->device->physical_device->info->props.has_abs_bin_mask;
   unsigned views = tu_fdm_num_layers(cmd);
   bool shared_viewport = cmd->state.rp.shared_viewport;
   uint32_t width = vsc->tile_count.width;

   for (uint32_t y = ty1; y < ty2; y++) {
      for (uint32_t x = tx1; x < tx2; x++) {
         struct tu_tile_config *tile =
            &tiles[width * y + x];
         if (tile->visible_views == 0)
            continue;
         if (x > tx1) {
            struct tu_tile_config *prev_x_tile = &tiles[width * y + x - 1];
            try_merge_tiles(tile, prev_x_tile, views, has_abs_mask,
                            shared_viewport);
         }
         if (y > ty1) {
            unsigned prev_y_idx = width * (y - 1) + x;
            struct tu_tile_config *prev_y_tile = &tiles[prev_y_idx];

            /* We can't merge prev_y_tile into tile if it's already been
             * merged horizontally into its neighbor in the previous row.
             */
            if (!prev_y_tile->merged_tile) {
               try_merge_tiles(tile, prev_y_tile, views, has_abs_mask,
                               shared_viewport);
            }
         }
      }
   }
}


struct tu_tile_config *
tu_calc_tile_config(struct tu_cmd_buffer *cmd, const struct tu_vsc_config *vsc,
                    const struct tu_image_view *fdm, const VkOffset2D *fdm_offsets)
{
   struct tu_tile_config *tiles = (struct tu_tile_config *)
      calloc(vsc->tile_count.width * vsc->tile_count.height,
             sizeof(struct tu_tile_config));

   for (uint32_t py = 0; py < vsc->pipe_count.height; py++) {
      uint32_t ty1 = py * vsc->pipe0.height;
      uint32_t ty2 = MIN2(ty1 + vsc->pipe0.height, vsc->tile_count.height);
      for (uint32_t px = 0; px < vsc->pipe_count.width; px++) {
         uint32_t tx1 = px * vsc->pipe0.width;
         uint32_t tx2 = MIN2(tx1 + vsc->pipe0.width, vsc->tile_count.width);
         uint32_t pipe_width = tx2 - tx1;
         uint32_t pipe = py * vsc->pipe_count.width + px;

         /* Initialize tiles and sample fragment density map */
         for (uint32_t y = ty1; y < ty2; y++) {
            for (uint32_t x = tx1; x < tx2; x++) {
               uint32_t tx = x - tx1;
               uint32_t ty = y - ty1;
               struct tu_tile_config *tile = &tiles[vsc->tile_count.width * y + x];

               tile->pos = { x, y };
               tile->sysmem_extent = { 1, 1 };
               tile->gmem_extent = { 1, 1 };
               tile->pipe = pipe;
               tile->slot_mask = 1u << (pipe_width * ty + tx);
               tile->merged_tile = NULL;
               tu_calc_bin_visibility(cmd, tile, fdm_offsets);
               tu_calc_frag_area(cmd, tile, fdm, fdm_offsets);
            }
         }

         /* Merge tiles */
         /* TODO: we should also be able to merge tiles when only
          * per_view_render_areas is used without FDM. That requires using
          * another method to force disable draws since we don't want to force
          * the viewport to be re-emitted, like overriding the view mask. It
          * would also require disabling stores, and adding patchpoints for
          * CmdClearAttachments in secondaries or making it use the view mask.
          */
         if (!TU_DEBUG(NO_BIN_MERGING) &&
             cmd->device->physical_device->info->props.has_bin_mask) {
            tu_merge_tiles(cmd, vsc, tiles, tx1, ty1, tx2, ty2);
         }
      }
   }

   return tiles;
}

