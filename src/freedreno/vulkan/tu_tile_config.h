/*
 * Copyright © 2026 Valve Corporation.
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_common.h"

#ifndef TU_TILE_CONFIG_H
#define TU_TILE_CONFIG_H

struct tu_tile_config {
   VkOffset2D pos;
   uint32_t pipe;
   uint32_t slot_mask;
   uint32_t visible_views;

   /* The tile this tile was merged with. */
   struct tu_tile_config *merged_tile;

   /* For merged tiles, the extent in tiles when resolved to system memory.
    */
   VkExtent2D sysmem_extent;

   /* For merged tiles, the extent in tiles in GMEM. This can only be more
    * than 1 if there is extra free space from an unused view.
    */
   VkExtent2D gmem_extent;

   VkExtent2D frag_areas[MAX_VIEWS];
};

struct tu_tile_config *
tu_calc_tile_config(struct tu_cmd_buffer *cmd, const struct tu_vsc_config *vsc,
                    const struct tu_image_view *fdm, const VkOffset2D *fdm_offsets);

void
tu_calc_bin_visibility(struct tu_cmd_buffer *cmd,
                       struct tu_tile_config *tile,
                       const VkOffset2D *offsets);

#endif
