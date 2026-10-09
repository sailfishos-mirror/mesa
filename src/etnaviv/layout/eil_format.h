/*
 * Copyright © 2026 Daniel Lang
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/format/u_formats.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum eil_emulation {
   EIL_EMU_NONE,
   EIL_EMU_DEPTH32F,
};

struct eil_format {
   uint32_t vtx;        /* FE_DATA_TYPE_* */
   uint32_t tex;        /* TEXTURE_FORMAT_* */
   uint32_t pe;         /* PE_FORMAT_* */
   enum eil_emulation emulation;
   enum pipe_format emulated;       /* enum pipe_format the HW really sees */

   bool has_vtx : 1;
   bool has_tex : 1;
   bool has_pe : 1;
   bool tex_ext : 1;    /* TEXTURE_FORMAT_EXT_* space */
   bool tex_astc : 1;   /* TEXTURE_FORMAT_ASTC_* space */
   bool pe_rb_swap : 1;
};

extern const struct eil_format eil_format_table[PIPE_FORMAT_COUNT];

static inline const struct eil_format *
eil_get_format_entry(enum pipe_format fmt)
{
   assert(fmt >= 0 && fmt < PIPE_FORMAT_COUNT);
   return &eil_format_table[fmt];
}

static inline bool
eil_format_is_emulated_z32f(enum pipe_format fmt)
{
   const struct eil_format *f = eil_get_format_entry(fmt);
   return f->emulation == EIL_EMU_DEPTH32F;
}

#ifdef __cplusplus
}
#endif
