/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Jordan Justen <jordan.l.justen@intel.com>
 *
 */

#include "main/bufferobj.h"
#include "main/varray.h"
#include "vbo/vbo.h"

#include "brw_context.h"
#include "brw_defines.h"
#include "brw_draw.h"

#include "brw_batch.h"

/**
 * Check if the hardware's cut index support can handle the primitive
 * restart index value (pre-Haswell only).
 */
static bool
can_cut_index_handle_restart_index(struct gl_context *ctx,
                                   const struct _mesa_index_buffer *ib,
                                   unsigned restart_index)
{
   /* The FixedIndex variant means 0xFF, 0xFFFF, or 0xFFFFFFFF based on
    * the index buffer type, which corresponds exactly to the hardware.
    */
   if (ctx->Array.PrimitiveRestartFixedIndex)
      return true;

   bool cut_index_will_work;

   switch (ib->index_size_shift) {
   case 0:
      cut_index_will_work = restart_index == 0xff;
      break;
   case 1:
      cut_index_will_work = restart_index == 0xffff;
      break;
   case 2:
      cut_index_will_work = restart_index == 0xffffffff;
      break;
   default:
      unreachable("not reached");
   }

   return cut_index_will_work;
}

/**
 * Check if the hardware's cut index support can handle the primitive
 * restart case.
 */
static bool
can_cut_index_handle_prims(struct gl_context *ctx,
                           const struct _mesa_prim *prim,
                           GLuint nr_prims,
                           const struct _mesa_index_buffer *ib,
                           unsigned restart_index)
{
   struct brw_context *brw = brw_context(ctx);
   const struct gen_device_info *devinfo = &brw->screen->devinfo;

   /* Otherwise Haswell can do it all. */
   if (devinfo->gen >= 8 || devinfo->is_haswell)
      return true;

   if (!can_cut_index_handle_restart_index(ctx, ib, restart_index)) {
      /* The primitive restart index can't be handled, so take
       * the software path
       */
      return false;
   }

   for (unsigned i = 0; i < nr_prims; i++) {
      switch (prim[i].mode) {
      case GL_POINTS:
      case GL_LINES:
      case GL_LINE_STRIP:
      case GL_TRIANGLES:
      case GL_TRIANGLE_STRIP:
      case GL_LINES_ADJACENCY:
      case GL_LINE_STRIP_ADJACENCY:
      case GL_TRIANGLES_ADJACENCY:
      case GL_TRIANGLE_STRIP_ADJACENCY:
         /* Cut index supports these primitive types */
         break;
      default:
         /* Cut index does not support these primitive types */
      //case GL_LINE_LOOP:
      //case GL_TRIANGLE_FAN:
      //case GL_QUADS:
      //case GL_QUAD_STRIP:
      //case GL_POLYGON:
         return false;
      }
   }

   return true;
}

/**
 * Check if primitive restart is enabled, and if so, handle it properly.
 *
 * In some cases the support will be handled in software. When available
 * hardware will handle primitive restart.
 */
GLboolean
brw_handle_primitive_restart(struct gl_context *ctx,
                             const struct _mesa_prim *prims,
                             GLuint nr_prims,
                             const struct _mesa_index_buffer *ib,
                             GLuint num_instances, GLuint base_instance,
                             bool primitive_restart,
                             unsigned restart_index)
{
   struct brw_context *brw = brw_context(ctx);

   /* We only need to handle cases where there is an index buffer. */
   if (ib == NULL) {
      return GL_FALSE;
   }

   /* If we have set the in_progress flag, then we are in the middle
    * of handling the primitive restart draw.
    */
   if (brw->prim_restart.in_progress) {
      return GL_FALSE;
   }

   /* If PrimitiveRestart is not enabled, then we aren't concerned about
    * handling this draw.
    */
   if (!primitive_restart) {
      return GL_FALSE;
   }

   /* Signal that we are in the process of handling the
    * primitive restart draw
    */
   brw->prim_restart.in_progress = true;

   if (can_cut_index_handle_prims(ctx, prims, nr_prims, ib, restart_index)) {
      /* Cut index should work for primitive restart, so use it
       */
      brw->prim_restart.enable_cut_index = true;
      brw->prim_restart.restart_index = restart_index;
      brw_draw_prims(ctx, prims, nr_prims, ib, false, primitive_restart,
                     restart_index, -1, -1,
                     num_instances, base_instance);
      brw->prim_restart.enable_cut_index = false;
   } else {
      /* Not all the primitive draw modes are supported by the cut index,
       * so take the software path
       */
      struct gl_buffer_object *indirect_data = brw->draw.draw_indirect_data;

      /* Clear this to make the draw direct. */
      brw->draw.draw_indirect_data = NULL;

      vbo_sw_primitive_restart(ctx, prims, nr_prims, ib, num_instances,
                               base_instance, indirect_data,
                               brw->draw.draw_indirect_offset,
                               primitive_restart, restart_index);
   }

   brw->prim_restart.in_progress = false;

   /* The primitive restart draw was completed, so return true. */
   return GL_TRUE;
}
