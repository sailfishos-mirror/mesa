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
union r300_shader_code {
   struct r300_fragment_shader_code *f;
   struct r300_vertex_shader_code *v;
};

const void *
nir_to_rc(struct nir_shader *s, struct pipe_screen *screen,
          struct r300_fragment_program_external_state state,
          union r300_shader_code rc);

void
ntr_fixup_varying_slots(struct nir_shader *s, nir_variable_mode mode);

/* Helpers used for translating between TGSI and RC enums. */

static inline unsigned
rc_translate_opcode(unsigned tgsi_opcode)
{
   switch (tgsi_opcode) {
   case TGSI_OPCODE_ARL:     return RC_OPCODE_ARL;
   case TGSI_OPCODE_MOV:     return RC_OPCODE_MOV;
   case TGSI_OPCODE_RCP:     return RC_OPCODE_RCP;
   case TGSI_OPCODE_RSQ:     return RC_OPCODE_RSQ;
   case TGSI_OPCODE_EXP:     return RC_OPCODE_EXP;
   case TGSI_OPCODE_LOG:     return RC_OPCODE_LOG;
   case TGSI_OPCODE_MUL:     return RC_OPCODE_MUL;
   case TGSI_OPCODE_ADD:     return RC_OPCODE_ADD;
   case TGSI_OPCODE_DP3:     return RC_OPCODE_DP3;
   case TGSI_OPCODE_DP4:     return RC_OPCODE_DP4;
   case TGSI_OPCODE_DST:     return RC_OPCODE_DST;
   case TGSI_OPCODE_MIN:     return RC_OPCODE_MIN;
   case TGSI_OPCODE_MAX:     return RC_OPCODE_MAX;
   case TGSI_OPCODE_SLT:     return RC_OPCODE_SLT;
   case TGSI_OPCODE_SGE:     return RC_OPCODE_SGE;
   case TGSI_OPCODE_MAD:     return RC_OPCODE_MAD;
   case TGSI_OPCODE_FRC:     return RC_OPCODE_FRC;
   case TGSI_OPCODE_ROUND:   return RC_OPCODE_ROUND;
   case TGSI_OPCODE_EX2:     return RC_OPCODE_EX2;
   case TGSI_OPCODE_LG2:     return RC_OPCODE_LG2;
   case TGSI_OPCODE_POW:     return RC_OPCODE_POW;
   case TGSI_OPCODE_COS:     return RC_OPCODE_COS;
   case TGSI_OPCODE_DDX:     return RC_OPCODE_DDX;
   case TGSI_OPCODE_DDY:     return RC_OPCODE_DDY;
   case TGSI_OPCODE_KILL:    return RC_OPCODE_KILP;
   case TGSI_OPCODE_SEQ:     return RC_OPCODE_SEQ;
   case TGSI_OPCODE_SIN:     return RC_OPCODE_SIN;
   case TGSI_OPCODE_SNE:     return RC_OPCODE_SNE;
   case TGSI_OPCODE_TEX:     return RC_OPCODE_TEX;
   case TGSI_OPCODE_TXD:     return RC_OPCODE_TXD;
   case TGSI_OPCODE_TXP:     return RC_OPCODE_TXP;
   case TGSI_OPCODE_ARR:     return RC_OPCODE_ARR;
   case TGSI_OPCODE_CMP:     return RC_OPCODE_CMP;
   case TGSI_OPCODE_TXB:     return RC_OPCODE_TXB;
   case TGSI_OPCODE_DP2:     return RC_OPCODE_DP2;
   case TGSI_OPCODE_TXL:     return RC_OPCODE_TXL;
   case TGSI_OPCODE_BRK:     return RC_OPCODE_BRK;
   case TGSI_OPCODE_IF:      return RC_OPCODE_IF;
   case TGSI_OPCODE_BGNLOOP: return RC_OPCODE_BGNLOOP;
   case TGSI_OPCODE_ELSE:    return RC_OPCODE_ELSE;
   case TGSI_OPCODE_ENDIF:   return RC_OPCODE_ENDIF;
   case TGSI_OPCODE_ENDLOOP: return RC_OPCODE_ENDLOOP;
   case TGSI_OPCODE_CONT:    return RC_OPCODE_CONT;
   case TGSI_OPCODE_NOP:     return RC_OPCODE_NOP;
   case TGSI_OPCODE_KILL_IF: return RC_OPCODE_KIL;
   }
   fprintf(stderr, "r300: Unknown TGSI/RC opcode: %u\n", tgsi_opcode);
   return RC_OPCODE_ILLEGAL_OPCODE;
}

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

static inline unsigned
rc_translate_tex_target(unsigned tgsi_target)
{
   switch (tgsi_target) {
   case TGSI_TEXTURE_1D:       return RC_TEXTURE_1D;
   case TGSI_TEXTURE_2D:       return RC_TEXTURE_2D;
   case TGSI_TEXTURE_3D:       return RC_TEXTURE_3D;
   case TGSI_TEXTURE_CUBE:     return RC_TEXTURE_CUBE;
   case TGSI_TEXTURE_RECT:     return RC_TEXTURE_RECT;
   case TGSI_TEXTURE_1D_ARRAY: return RC_TEXTURE_1D_ARRAY;
   case TGSI_TEXTURE_2D_ARRAY: return RC_TEXTURE_2D_ARRAY;
   default:
      UNREACHABLE("unsupported tex target");
   }
}

#endif /* NIR_TO_RC_H */
