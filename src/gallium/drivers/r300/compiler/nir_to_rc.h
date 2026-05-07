/*
 * Copyright © 2014 Broadcom
 * SPDX-License-Identifier: MIT
 */

#ifndef NIR_TO_RC_H
#define NIR_TO_RC_H

#include <stdbool.h>
#include <stdio.h>
#include "compiler/nir/nir.h"
#include "pipe/p_defines.h"
#include "r300_fs.h"
#include "r300_shader_semantics.h"
#include "r300_vs.h"

#include "compiler/radeon_program_constants.h"
#include "pipe/p_shader_tokens.h"
#include "util/compiler.h"

struct nir_shader;
struct pipe_screen;
struct r300_fragment_program_external_state;
struct radeon_compiler;
union r300_shader_code {
   struct r300_fragment_shader_code *f;
   struct r300_vertex_shader_code *v;
};

void
nir_to_rc(struct nir_shader *s, struct pipe_screen *screen,
          struct r300_fragment_program_external_state state,
          union r300_shader_code rc, struct radeon_compiler *compiler);

void
ntr_fixup_varying_slots(struct nir_shader *s, nir_variable_mode mode);

/* Helpers used for translating between TGSI and RC enums. */

static inline unsigned
rc_translate_register_file(unsigned tgsi_file)
{
   switch (tgsi_file) {
   case TGSI_FILE_CONSTANT:  return RC_FILE_CONSTANT;
   case TGSI_FILE_IMMEDIATE: return RC_FILE_CONSTANT;
   case TGSI_FILE_INPUT:     return RC_FILE_INPUT;
   case TGSI_FILE_OUTPUT:    return RC_FILE_OUTPUT;
   case TGSI_FILE_ADDRESS:   return RC_FILE_ADDRESS;
   default:
      fprintf(stderr, "Unhandled register file: %i\n", tgsi_file);
      FALLTHROUGH;
   case TGSI_FILE_TEMPORARY: return RC_FILE_TEMPORARY;
   }
}

static inline unsigned
rc_translate_saturate(bool saturate)
{
   return saturate ? RC_SATURATE_ZERO_ONE : RC_SATURATE_NONE;
}

#endif /* NIR_TO_RC_H */
