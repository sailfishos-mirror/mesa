/*
 * Copyright (C) 2026 Amazon.com, Inc. or its affiliates
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_BLITTER_H__
#define __PAN_BLITTER_H__

#include "pan_context.h"

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

struct blitter_context *panfrost_blitter_create(struct pipe_context *pipe);

void panfrost_blitter_save(struct panfrost_context *ctx,
                           const enum panfrost_blitter_op blitter_op);

/* Callers should ensure that all AFBC/AFRC resources that will be used in the
 * blit operation are legalized before calling blitter operations, otherwise
 * we may trigger a recursive blit */
void panfrost_blitter_blit_no_afbc_legalization(struct pipe_context *pipe,
                                                const struct pipe_blit_info *info);

void panfrost_blitter_blit(struct pipe_context *pipe,
                           const struct pipe_blit_info *info);

#endif
