/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "freedreno_surface.h"
#include "freedreno_resource.h"
#include "freedreno_util.h"

#include "util/u_inlines.h"
#include "util/u_memory.h"

struct pipe_surface *
fd_create_surface(struct pipe_context *pctx, struct pipe_resource *ptex,
                  const struct pipe_surface *surf_tmpl)
{
   struct pipe_surface *psurf = CALLOC_STRUCT(pipe_surface);

   if (!psurf)
      return NULL;

   unsigned level = surf_tmpl->u.tex.level;

   pipe_reference_init(&psurf->reference, 1);
   pipe_resource_reference(&psurf->texture, ptex);

   psurf->context = pctx;
   psurf->format = surf_tmpl->format;
   psurf->nr_samples = surf_tmpl->nr_samples;

   if (ptex->target == PIPE_BUFFER) {
      psurf->u.buf.first_element = surf_tmpl->u.buf.first_element;
      psurf->u.buf.last_element = surf_tmpl->u.buf.last_element;
   } else {
      psurf->u.tex.level = level;
      psurf->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
      psurf->u.tex.last_layer = surf_tmpl->u.tex.last_layer;
   }

   return psurf;
}

void
fd_surface_destroy(struct pipe_context *pctx, struct pipe_surface *psurf)
{
   pipe_resource_reference(&psurf->texture, NULL);
   FREE(psurf);
}
