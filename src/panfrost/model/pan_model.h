/*
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2026 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_MODEL_H
#define PAN_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "util/macros.h"

/** Implementation-defined tiler features */
struct pan_tiler_features {
   /** Number of bytes per tiler bin */
   unsigned bin_size;

   /** Maximum number of levels that may be simultaneously enabled.
    * Invariant: bitcount(hierarchy_mask) <= max_levels */
   unsigned max_levels;
};

#define MIDGARD_PRODUCT_ID(x) (((x) & BITFIELD_RANGE(16, 16)) >> 16)

#define PAN_ARCH_MAJOR(x)    (((x) & BITFIELD_RANGE(28, 4)) >> 28)
#define PAN_ARCH_MINOR(x)    (((x) & BITFIELD_RANGE(24, 4)) >> 24)
#define PAN_ARCH_REV(x)      (((x) & BITFIELD_RANGE(20, 4)) >> 20)
#define PAN_PRODUCT_MAJOR(x) (((x) & BITFIELD_RANGE(16, 4)) >> 16)

#define PAN_VERSION_MAJOR(x)  (((x) & BITFIELD_RANGE(12, 4)) >> 12)
#define PAN_VERSION_MINOR(x)  (((x) & BITFIELD_RANGE(4, 8)) >> 4)
#define PAN_VERSION_STATUS(x) ((x) & BITFIELD_RANGE(0, 4))

struct pan_model {
   /* GPU product ID */
   uint32_t gpu_prod_id;

   /* GPU variant. */
   uint32_t gpu_variant;

   /* Marketing name for the GPU, used as the GL_RENDERER */
   const char *name;

   /* Set of associated performance counters */
   const char *performance_counters;

   /* Minimum GPU revision required for anisotropic filtering. ~0 and 0
    * means "no revisions support anisotropy" and "all revisions support
    * anistropy" respectively -- so checking for anisotropy is simply
    * comparing the reivsion.
    */
   uint32_t min_rev_anisotropic;

   struct {
      /* Default tilebuffer size in bytes for the model. */
      uint32_t color_size;

      /* Default tilebuffer depth size in bytes for the model. */
      uint32_t z_size;
   } tilebuffer;

   /* Maximum number of pixels, texels, and FMA ops, per clock per shader
    * core, or 0 if it can't be determined for the given GPU. */
   struct {
      uint32_t pixel;
      uint32_t texel;
      uint32_t fma;
   } rates;

   struct {
      /* The GPU lacks the capability for hierarchical tiling, without
       * an "Advanced Tiling Unit", instead requiring a single bin
       * size for the entire framebuffer be selected by the driver
       */
      bool no_hierarchical_tiling;
      bool max_4x_msaa;
   } quirks;
};

const struct pan_model *pan_get_model(uint64_t gpu_id, uint32_t gpu_variant);

/* Returns the architecture version given a GPU ID, either from a table for
 * old-style Midgard versions or directly for new-style Bifrost/Valhall
 * versions */

static inline unsigned
pan_arch(uint64_t gpu_id)
{
   switch (MIDGARD_PRODUCT_ID(gpu_id)) {
   case 0x600:
   case 0x620:
   case 0x720:
      return 4;
   case 0x750:
   case 0x820:
   case 0x830:
   case 0x860:
   case 0x880:
      return 5;
   default:
      return PAN_ARCH_MAJOR(gpu_id);
   }
}

#endif
