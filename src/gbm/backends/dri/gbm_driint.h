/*
 * Copyright © 2011 Intel Corporation
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Benjamin Franzke <benjaminfranzke@googlemail.com>
 */

#ifndef _GBM_DRI_INTERNAL_H_
#define _GBM_DRI_INTERNAL_H_

#include <xf86drm.h>
#include <string.h>
#include <sys/mman.h>
#include <gbm_backend_abi.h>
#include "c11/threads.h"

#include <GL/gl.h> /* mesa_interface needs GL types */
#include "mesa_interface.h"
#include "kopper_interface.h"

struct gbm_dri_surface;
struct gbm_dri_bo;

struct gbm_dri_visual {
   uint32_t gbm_format;
   int pipe_format;
};

struct gbm_dri_device {
   struct gbm_device base;

   char *driver_name; /* Name of the DRI module, without the _dri suffix */
   bool software; /* A software driver was loaded */
   bool swrast; /* this is swrast */
   bool has_dmabuf_import;
   bool has_dmabuf_export;
   bool has_compression_modifiers;

   struct dri_screen *screen;
   struct dri_context *context;
   mtx_t mutex;

   const struct dri_config   **driver_configs;
   const __DRIextension **loader_extensions;

   GLboolean (*validate_image)(void *image, void *data);
   struct dri_image *(*lookup_image_validated)(void *image, void *data);
   void *lookup_user_data;

   void (*flush_front_buffer)(struct dri_drawable * driDrawable, void *data);
   int (*image_get_buffers)(struct dri_drawable *driDrawable,
                            unsigned int format,
                            uint32_t *stamp,
                            void *loaderPrivate,
                            uint32_t buffer_mask,
                            struct __DRIimageList *buffers);
   void (*swrast_put_image2)(struct dri_drawable *driDrawable,
                             int            op,
                             int            x,
                             int            y,
                             int            width,
                             int            height,
                             int            stride,
                             char          *data,
                             void          *loaderPrivate);
   void (*swrast_get_image)(struct dri_drawable *driDrawable,
                            int            x,
                            int            y,
                            int            width,
                            int            height,
                            char          *data,
                            void          *loaderPrivate);

   struct wl_drm *wl_drm;

   const struct gbm_dri_visual *visual_table;
   int num_visuals;
};

struct gbm_dri_bo {
   struct gbm_bo base;

   struct dri_image *image;

   /* Used for cursors and the swrast front BO */
   uint32_t handle, size;
   void *map;
};

struct gbm_dri_surface {
   struct gbm_surface base;

   void *dri_private;
};

static inline struct gbm_dri_device *
gbm_dri_device(struct gbm_device *gbm)
{
   return (struct gbm_dri_device *) gbm;
}

static inline struct gbm_dri_bo *
gbm_dri_bo(struct gbm_bo *bo)
{
   return (struct gbm_dri_bo *) bo;
}

static inline struct gbm_dri_surface *
gbm_dri_surface(struct gbm_surface *surface)
{
   return (struct gbm_dri_surface *) surface;
}

static inline void *
gbm_dri_bo_map_dumb(struct gbm_dri_bo *bo)
{
   struct drm_mode_map_dumb map_arg;
   int ret;

   if (bo->image != NULL)
      return NULL;

   if (bo->map != NULL)
      return bo->map;

   memset(&map_arg, 0, sizeof(map_arg));
   map_arg.handle = bo->handle;

   ret = drmIoctl(bo->base.gbm->v0.fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg);
   if (ret)
      return NULL;

   bo->map = mmap(NULL, bo->size, PROT_WRITE,
                  MAP_SHARED, bo->base.gbm->v0.fd, map_arg.offset);
   if (bo->map == MAP_FAILED) {
      bo->map = NULL;
      return NULL;
   }

   return bo->map;
}

static inline void
gbm_dri_bo_unmap_dumb(struct gbm_dri_bo *bo)
{
   munmap(bo->map, bo->size);
   bo->map = NULL;
}

#endif
