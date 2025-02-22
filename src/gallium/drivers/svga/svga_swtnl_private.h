/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_SWTNL_PRIVATE_H
#define SVGA_SWTNL_PRIVATE_H

#include "svga_swtnl.h"
#include "draw/draw_vertex.h"

#include "svga3d_reg.h"

/**
 * Primitive renderer for svga.
 */
struct svga_vbuf_render {
   struct vbuf_render base;

   struct svga_context *svga;
   struct vertex_info vertex_info;

   unsigned vertex_size;

   SVGA3dElementLayoutId layout_id; /**< current element layout id */

   enum mesa_prim prim;

   struct pipe_resource *vbuf;
   struct pipe_resource *ibuf;
   struct pipe_transfer *vbuf_transfer;
   struct pipe_transfer *ibuf_transfer;

   void *vbuf_ptr;

   /* current size of buffer */
   size_t vbuf_size;
   size_t ibuf_size;

   /* size of that the buffer should be */
   size_t vbuf_alloc_size;
   size_t ibuf_alloc_size;

   /* current write place */
   size_t vbuf_offset;
   size_t ibuf_offset;

   /* currently used */
   size_t vbuf_used;

   SVGA3dVertexDecl vdecl[PIPE_MAX_ATTRIBS];
   unsigned vdecl_offset;
   unsigned vdecl_count;

   uint16_t min_index;
   uint16_t max_index;
};

/**
 * Basically a cast wrapper.
 */
static inline struct svga_vbuf_render *
svga_vbuf_render( struct vbuf_render *render )
{
   assert(render);
   return (struct svga_vbuf_render *)render;
}


struct vbuf_render *
svga_vbuf_render_create( struct svga_context *svga );


enum pipe_error
svga_swtnl_update_vdecl( struct svga_context *svga );


#endif
