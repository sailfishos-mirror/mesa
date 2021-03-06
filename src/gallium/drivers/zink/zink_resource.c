/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_resource.h"

#include "zink_batch.h"
#include "zink_context.h"
#include "zink_program.h"
#include "zink_screen.h"

#include "vulkan/wsi/wsi_common.h"

#include "util/slab.h"
#include "util/u_debug.h"
#include "util/format/u_format.h"
#include "util/u_transfer_helper.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

#include "frontend/sw_winsys.h"

#ifndef _WIN32
#define ZINK_USE_DMABUF
#endif

#ifdef ZINK_USE_DMABUF
#include "drm-uapi/drm_fourcc.h"
#endif

static void
resource_sync_writes_from_batch_id(struct zink_context *ctx, uint32_t batch_uses, unsigned cur_batch)
{
   if (batch_uses & ZINK_RESOURCE_ACCESS_WRITE << ZINK_COMPUTE_BATCH_ID)
      zink_wait_on_batch(ctx, ZINK_COMPUTE_BATCH_ID);
   batch_uses &= ~(ZINK_RESOURCE_ACCESS_WRITE << ZINK_COMPUTE_BATCH_ID);
   if (!batch_uses)
      return;
   for (unsigned i = 0; i < ZINK_NUM_BATCHES + 1; i++) {
      /* loop backwards and sync with highest batch id that has writes */
      if (batch_uses & (ZINK_RESOURCE_ACCESS_WRITE << cur_batch)) {
          zink_wait_on_batch(ctx, cur_batch);
          break;
      }
      cur_batch--;
      if (cur_batch > ZINK_COMPUTE_BATCH_ID - 1) // underflowed past max batch id
         cur_batch = ZINK_COMPUTE_BATCH_ID - 1;
   }
}

static uint32_t
mem_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct mem_key));
}

static bool
mem_equals(const void *a, const void *b)
{
   return !memcmp(a, b, sizeof(struct mem_key));
}

static void
cache_or_free_mem(struct zink_screen *screen, struct zink_resource *res)
{
   if (res->mkey.flags) {
      simple_mtx_lock(&screen->mem_cache_mtx);
      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(screen->resource_mem_cache, res->mem_hash, &res->mkey);
      struct util_dynarray *array = he ? (void*)he->data : NULL;
      if (!array) {
         struct mem_key *mkey = rzalloc(screen->resource_mem_cache, struct mem_key);
         memcpy(mkey, &res->mkey, sizeof(struct mem_key));
         array = rzalloc(screen->resource_mem_cache, struct util_dynarray);
         util_dynarray_init(array, screen->resource_mem_cache);
         _mesa_hash_table_insert_pre_hashed(screen->resource_mem_cache, res->mem_hash, mkey, array);
      }
      if (util_dynarray_num_elements(array, VkDeviceMemory) < 5) {
         util_dynarray_append(array, VkDeviceMemory, res->mem);
         simple_mtx_unlock(&screen->mem_cache_mtx);
         return;
      }
      simple_mtx_unlock(&screen->mem_cache_mtx);
   }
   vkFreeMemory(screen->dev, res->mem, NULL);
}

static void
zink_resource_destroy(struct pipe_screen *pscreen,
                      struct pipe_resource *pres)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = zink_resource(pres);
   if (pres->target == PIPE_BUFFER) {
      vkDestroyBuffer(screen->dev, res->buffer, NULL);
      util_range_destroy(&res->valid_buffer_range);
   } else
      vkDestroyImage(screen->dev, res->image, NULL);

   zink_descriptor_set_refs_clear(&res->desc_set_refs, res);

   cache_or_free_mem(screen, res);

   FREE(res);
}

static uint32_t
get_memory_type_index(struct zink_screen *screen,
                      const VkMemoryRequirements *reqs,
                      VkMemoryPropertyFlags props)
{
   for (uint32_t i = 0u; i < VK_MAX_MEMORY_TYPES; i++) {
      if (((reqs->memoryTypeBits >> i) & 1) == 1) {
         if ((screen->info.mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
            break;
         }
      }
   }

   unreachable("Unsupported memory-type");
   return 0;
}

static VkImageAspectFlags
aspect_from_format(enum pipe_format fmt)
{
   if (util_format_is_depth_or_stencil(fmt)) {
      VkImageAspectFlags aspect = 0;
      const struct util_format_description *desc = util_format_description(fmt);
      if (util_format_has_depth(desc))
         aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
      if (util_format_has_stencil(desc))
         aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
      return aspect;
   } else
     return VK_IMAGE_ASPECT_COLOR_BIT;
}

static struct pipe_resource *
resource_create(struct pipe_screen *pscreen,
                const struct pipe_resource *templ,
                struct winsys_handle *whandle,
                unsigned external_usage)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = CALLOC_STRUCT(zink_resource);

   res->base = *templ;

   pipe_reference_init(&res->base.reference, 1);
   res->base.screen = pscreen;

   VkMemoryRequirements reqs = {};
   VkMemoryPropertyFlags flags;

   res->internal_format = templ->format;
   if (templ->target == PIPE_BUFFER) {
      VkBufferCreateInfo bci = {};
      bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bci.size = templ->width0;

      bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

      if (templ->usage != PIPE_USAGE_STAGING)
         bci.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

      /* apparently gallium thinks these are the jack-of-all-trades bind types */
      if (templ->bind & (PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_QUERY_BUFFER)) {
         bci.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
         VkFormatProperties props = screen->format_props[templ->format];
         if (props.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT)
            bci.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
      }

      if (templ->bind & PIPE_BIND_VERTEX_BUFFER)
         bci.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;

      if (templ->bind & PIPE_BIND_INDEX_BUFFER)
         bci.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

      if (templ->bind & PIPE_BIND_CONSTANT_BUFFER)
         bci.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

      if (templ->bind & PIPE_BIND_SHADER_BUFFER)
         bci.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

      if (templ->bind & PIPE_BIND_COMMAND_ARGS_BUFFER)
         bci.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

      if (templ->bind == (PIPE_BIND_STREAM_OUTPUT | PIPE_BIND_CUSTOM)) {
         bci.usage |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
      } else if (templ->bind & PIPE_BIND_STREAM_OUTPUT) {
         bci.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
      }

      if (vkCreateBuffer(screen->dev, &bci, NULL, &res->buffer) !=
          VK_SUCCESS) {
         debug_printf("vkCreateBuffer failed\n");
         FREE(res);
         return NULL;
      }

      vkGetBufferMemoryRequirements(screen->dev, res->buffer, &reqs);
      flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
   } else {
      res->format = zink_get_format(screen, templ->format);

      VkImageCreateInfo ici = {};
      VkExternalMemoryImageCreateInfo emici = {};
      ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      ici.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

      switch (templ->target) {
      case PIPE_TEXTURE_1D:
      case PIPE_TEXTURE_1D_ARRAY:
         ici.imageType = VK_IMAGE_TYPE_1D;
         break;

      case PIPE_TEXTURE_CUBE:
      case PIPE_TEXTURE_CUBE_ARRAY:
         ici.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
         /* fall-through */
      case PIPE_TEXTURE_2D:
      case PIPE_TEXTURE_2D_ARRAY:
      case PIPE_TEXTURE_RECT:
         ici.imageType = VK_IMAGE_TYPE_2D;
         break;

      case PIPE_TEXTURE_3D:
         ici.imageType = VK_IMAGE_TYPE_3D;
         if (templ->bind & PIPE_BIND_RENDER_TARGET)
            ici.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
         break;

      case PIPE_BUFFER:
         unreachable("PIPE_BUFFER should already be handled");

      default:
         unreachable("Unknown target");
      }

      ici.format = res->format;
      ici.extent.width = templ->width0;
      ici.extent.height = templ->height0;
      ici.extent.depth = templ->depth0;
      ici.mipLevels = templ->last_level + 1;
      ici.arrayLayers = MAX2(templ->array_size, 1);
      ici.samples = templ->nr_samples ? templ->nr_samples : VK_SAMPLE_COUNT_1_BIT;
      ici.tiling = templ->bind & PIPE_BIND_LINEAR ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;

      if (templ->target == PIPE_TEXTURE_CUBE ||
          templ->target == PIPE_TEXTURE_CUBE_ARRAY)
         ici.arrayLayers *= 6;

      if (templ->bind & PIPE_BIND_SHARED) {
         emici.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
         emici.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
         ici.pNext = &emici;

         /* TODO: deal with DRM modifiers here */
         ici.tiling = VK_IMAGE_TILING_LINEAR;
      }

      if (templ->usage == PIPE_USAGE_STAGING)
         ici.tiling = VK_IMAGE_TILING_LINEAR;

      /* sadly, gallium doesn't let us know if it'll ever need this, so we have to assume */
      ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                  VK_IMAGE_USAGE_SAMPLED_BIT;

      if ((templ->nr_samples <= 1 || screen->info.feats.features.shaderStorageImageMultisample) &&
          (templ->bind & PIPE_BIND_SHADER_IMAGE ||
          (templ->bind & PIPE_BIND_SAMPLER_VIEW && templ->flags & PIPE_RESOURCE_FLAG_TEXTURING_MORE_LIKELY))) {
         VkFormatProperties props = screen->format_props[templ->format];
         /* gallium doesn't provide any way to actually know whether this will be used as a shader image,
          * so we have to just assume and set the bit if it's available
          */
         if ((ici.tiling == VK_IMAGE_TILING_LINEAR && props.linearTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
             (ici.tiling == VK_IMAGE_TILING_OPTIMAL && props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
            ici.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
      }

      if (templ->bind & PIPE_BIND_RENDER_TARGET)
         ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

      if (templ->bind & PIPE_BIND_DEPTH_STENCIL)
         ici.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

      if (templ->flags & PIPE_RESOURCE_FLAG_SPARSE)
         ici.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

      if (templ->bind & PIPE_BIND_STREAM_OUTPUT)
         ici.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

      ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      res->layout = VK_IMAGE_LAYOUT_UNDEFINED;

      struct wsi_image_create_info image_wsi_info = {
         VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA,
         NULL,
         .scanout = true,
      };

      if (screen->needs_mesa_wsi && (templ->bind & PIPE_BIND_SCANOUT))
         ici.pNext = &image_wsi_info;

      VkResult result = vkCreateImage(screen->dev, &ici, NULL, &res->image);
      if (result != VK_SUCCESS) {
         debug_printf("vkCreateImage failed\n");
         FREE(res);
         return NULL;
      }

      res->optimal_tiling = ici.tiling != VK_IMAGE_TILING_LINEAR;
      res->aspect = aspect_from_format(templ->format);

      vkGetImageMemoryRequirements(screen->dev, res->image, &reqs);
      if (templ->usage == PIPE_USAGE_STAGING)
        flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      else
        flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   }

   if (templ->flags & PIPE_RESOURCE_FLAG_MAP_COHERENT)
      flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

   VkMemoryAllocateInfo mai = {};
   mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mai.allocationSize = reqs.size;
   mai.memoryTypeIndex = get_memory_type_index(screen, &reqs, flags);

   if (templ->target != PIPE_BUFFER) {
      VkMemoryType mem_type =
         screen->info.mem_props.memoryTypes[mai.memoryTypeIndex];
      res->host_visible = mem_type.propertyFlags &
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
   }

   VkExportMemoryAllocateInfo emai = {};
   if (templ->bind & PIPE_BIND_SHARED) {
      emai.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
      emai.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

      emai.pNext = mai.pNext;
      mai.pNext = &emai;
   }

   VkImportMemoryFdInfoKHR imfi = {
      VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      NULL,
   };

   if (whandle && whandle->type == WINSYS_HANDLE_TYPE_FD) {
      imfi.pNext = NULL;
      imfi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      imfi.fd = whandle->handle;

      imfi.pNext = mai.pNext;
      emai.pNext = &imfi;
   }

   struct wsi_memory_allocate_info memory_wsi_info = {
      VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA,
      NULL,
   };

   if (screen->needs_mesa_wsi && (templ->bind & PIPE_BIND_SCANOUT)) {
      memory_wsi_info.implicit_sync = true;

      memory_wsi_info.pNext = mai.pNext;
      mai.pNext = &memory_wsi_info;
   }

   if (!mai.pNext && !(templ->flags & PIPE_RESOURCE_FLAG_MAP_COHERENT)) {
      res->mkey.reqs = reqs;
      res->mkey.flags = flags;
      res->mem_hash = mem_hash(&res->mkey);
      simple_mtx_lock(&screen->mem_cache_mtx);

      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(screen->resource_mem_cache, res->mem_hash, &res->mkey);
      struct util_dynarray *array = he ? (void*)he->data : NULL;
      if (array && util_dynarray_num_elements(array, VkDeviceMemory)) {
         res->mem = util_dynarray_pop(array, VkDeviceMemory);
      }
      simple_mtx_unlock(&screen->mem_cache_mtx);
   }

   if (!res->mem && vkAllocateMemory(screen->dev, &mai, NULL, &res->mem) != VK_SUCCESS) {
      debug_printf("vkAllocateMemory failed\n");
      goto fail;
   }

   res->offset = 0;
   res->size = reqs.size;

   if (templ->target == PIPE_BUFFER) {
      vkBindBufferMemory(screen->dev, res->buffer, res->mem, res->offset);
      util_range_init(&res->valid_buffer_range);
   } else
      vkBindImageMemory(screen->dev, res->image, res->mem, res->offset);

   if (screen->winsys && (templ->bind & PIPE_BIND_DISPLAY_TARGET)) {
      struct sw_winsys *winsys = screen->winsys;
      res->dt = winsys->displaytarget_create(screen->winsys,
                                             res->base.bind,
                                             res->base.format,
                                             templ->width0,
                                             templ->height0,
                                             64, NULL,
                                             &res->dt_stride);
   }

   util_dynarray_init(&res->desc_set_refs.refs, NULL);
   return &res->base;

fail:
   if (templ->target == PIPE_BUFFER)
      vkDestroyBuffer(screen->dev, res->buffer, NULL);
   else
      vkDestroyImage(screen->dev, res->image, NULL);

   FREE(res);

   return NULL;
}

static struct pipe_resource *
zink_resource_create(struct pipe_screen *pscreen,
                     const struct pipe_resource *templ)
{
   return resource_create(pscreen, templ, NULL, 0);
}

static bool
zink_resource_get_handle(struct pipe_screen *pscreen,
                         struct pipe_context *context,
                         struct pipe_resource *tex,
                         struct winsys_handle *whandle,
                         unsigned usage)
{
   struct zink_resource *res = zink_resource(tex);
   struct zink_screen *screen = zink_screen(pscreen);

   if (res->base.target != PIPE_BUFFER) {
      VkImageSubresource sub_res = {};
      VkSubresourceLayout sub_res_layout = {};

      sub_res.aspectMask = res->aspect;

      vkGetImageSubresourceLayout(screen->dev, res->image, &sub_res, &sub_res_layout);

      whandle->stride = sub_res_layout.rowPitch;
   }

   if (whandle->type == WINSYS_HANDLE_TYPE_FD) {
#ifdef ZINK_USE_DMABUF
      VkMemoryGetFdInfoKHR fd_info = {};
      int fd;
      fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
      fd_info.memory = res->mem;
      fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      VkResult result = (*screen->vk_GetMemoryFdKHR)(screen->dev, &fd_info, &fd);
      if (result != VK_SUCCESS)
         return false;
      whandle->handle = fd;
      whandle->modifier = DRM_FORMAT_MOD_INVALID;
#else
      return false;
#endif
   }
   return true;
}

static struct pipe_resource *
zink_resource_from_handle(struct pipe_screen *pscreen,
                 const struct pipe_resource *templ,
                 struct winsys_handle *whandle,
                 unsigned usage)
{
#ifdef ZINK_USE_DMABUF
   if (whandle->modifier != DRM_FORMAT_MOD_INVALID)
      return NULL;

   return resource_create(pscreen, templ, whandle, usage);
#else
   return NULL;
#endif
}

static bool
zink_transfer_copy_bufimage(struct zink_context *ctx,
                            struct zink_resource *res,
                            struct zink_resource *staging_res,
                            struct zink_transfer *trans,
                            bool buf2img)
{
   struct zink_batch *batch = zink_batch_no_rp(ctx);

   if (buf2img) {
      zink_resource_image_barrier(ctx, batch, res, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0);
   } else {
      zink_resource_image_barrier(ctx, batch, res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0);
   }

   VkBufferImageCopy copyRegion = {};
   copyRegion.bufferOffset = staging_res->offset;
   copyRegion.bufferRowLength = 0;
   copyRegion.bufferImageHeight = 0;
   copyRegion.imageSubresource.mipLevel = trans->base.level;
   copyRegion.imageSubresource.layerCount = 1;
   if (res->base.array_size > 1) {
      copyRegion.imageSubresource.baseArrayLayer = trans->base.box.z;
      copyRegion.imageSubresource.layerCount = trans->base.box.depth;
      copyRegion.imageExtent.depth = 1;
   } else {
      copyRegion.imageOffset.z = trans->base.box.z;
      copyRegion.imageExtent.depth = trans->base.box.depth;
   }
   copyRegion.imageOffset.x = trans->base.box.x;
   copyRegion.imageOffset.y = trans->base.box.y;

   copyRegion.imageExtent.width = trans->base.box.width;
   copyRegion.imageExtent.height = trans->base.box.height;

   zink_batch_reference_resource_rw(batch, res, buf2img);
   zink_batch_reference_resource_rw(batch, staging_res, !buf2img);

   /* we're using u_transfer_helper_deinterleave, which means we'll be getting PIPE_MAP_* usage
    * to indicate whether to copy either the depth or stencil aspects
    */
   unsigned aspects = 0;
   assert((trans->base.usage & (PIPE_MAP_DEPTH_ONLY | PIPE_MAP_STENCIL_ONLY)) !=
          (PIPE_MAP_DEPTH_ONLY | PIPE_MAP_STENCIL_ONLY));
   if (trans->base.usage & PIPE_MAP_DEPTH_ONLY)
      aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
   else if (trans->base.usage & PIPE_MAP_STENCIL_ONLY)
      aspects = VK_IMAGE_ASPECT_STENCIL_BIT;
   else {
      aspects = aspect_from_format(res->base.format);
   }
   while (aspects) {
      int aspect = 1 << u_bit_scan(&aspects);
      copyRegion.imageSubresource.aspectMask = aspect;

      /* this may or may not work with multisampled depth/stencil buffers depending on the driver implementation:
       *
       * srcImage must have a sample count equal to VK_SAMPLE_COUNT_1_BIT
       * - vkCmdCopyImageToBuffer spec
       *
       * dstImage must have a sample count equal to VK_SAMPLE_COUNT_1_BIT
       * - vkCmdCopyBufferToImage spec
       */
      if (buf2img)
         vkCmdCopyBufferToImage(batch->cmdbuf, staging_res->buffer, res->image, res->layout, 1, &copyRegion);
      else
         vkCmdCopyImageToBuffer(batch->cmdbuf, res->image, res->layout, staging_res->buffer, 1, &copyRegion);
   }

   return true;
}

uint32_t
zink_get_resource_usage(struct zink_resource *res)
{
   uint32_t batch_uses = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(res->batch_uses); i++)
      batch_uses |= p_atomic_read(&res->batch_uses[i]) << i;
   return batch_uses;
}

static void *
zink_transfer_map(struct pipe_context *pctx,
                  struct pipe_resource *pres,
                  unsigned level,
                  unsigned usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **transfer)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(pres);
   uint32_t batch_uses = zink_get_resource_usage(res);

   struct zink_transfer *trans = slab_alloc(&ctx->transfer_pool);
   if (!trans)
      return NULL;

   memset(trans, 0, sizeof(*trans));
   pipe_resource_reference(&trans->base.resource, pres);

   trans->base.resource = pres;
   trans->base.level = level;
   trans->base.usage = usage;
   trans->base.box = *box;

   void *ptr;
   if (pres->target == PIPE_BUFFER) {
      if (!(usage & PIPE_MAP_UNSYNCHRONIZED)) {
         if (util_ranges_intersect(&res->valid_buffer_range, box->x, box->x + box->width)) {
            /* special case compute reads since they aren't handled by zink_fence_wait() */
            if (usage & PIPE_MAP_WRITE && (batch_uses & (ZINK_RESOURCE_ACCESS_READ << ZINK_COMPUTE_BATCH_ID)))
               zink_wait_on_batch(ctx, ZINK_COMPUTE_BATCH_ID);
            batch_uses &= ~(ZINK_RESOURCE_ACCESS_READ << ZINK_COMPUTE_BATCH_ID);
            if (usage & PIPE_MAP_READ && batch_uses >= ZINK_RESOURCE_ACCESS_WRITE)
               resource_sync_writes_from_batch_id(ctx, batch_uses, zink_curr_batch(ctx)->batch_id);
            else if (usage & PIPE_MAP_WRITE && batch_uses) {
               /* need to wait for all rendering to finish
                * TODO: optimize/fix this to be much less obtrusive
                * mesa/mesa#2966
                */

               trans->staging_res = pipe_buffer_create(pctx->screen, 0, PIPE_USAGE_STAGING, pres->width0);
               res = zink_resource(trans->staging_res);
            }
         }
         if (usage & PIPE_MAP_DISCARD_WHOLE_RESOURCE)
            util_range_set_empty(&res->valid_buffer_range);
      }


      VkResult result = vkMapMemory(screen->dev, res->mem, res->offset, res->size, 0, &ptr);
      if (result != VK_SUCCESS)
         return NULL;

#if defined(__APPLE__)
      if (!(usage & PIPE_MAP_DISCARD_WHOLE_RESOURCE)) {
         // Work around for MoltenVk limitation
         // MoltenVk returns blank memory ranges when there should be data present
         // This is a known limitation of MoltenVK.
         // See https://github.com/KhronosGroup/MoltenVK/blob/master/Docs/MoltenVK_Runtime_UserGuide.md#known-moltenvk-limitations
         VkMappedMemoryRange range = {
            VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            NULL,
            res->mem,
            res->offset,
            res->size
         };
         result = vkFlushMappedMemoryRanges(screen->dev, 1, &range);
         if (result != VK_SUCCESS)
            return NULL;
      }
#endif

      trans->base.stride = 0;
      trans->base.layer_stride = 0;
      ptr = ((uint8_t *)ptr) + box->x;
      if (usage & PIPE_MAP_WRITE)
         util_range_add(&res->base, &res->valid_buffer_range, box->x, box->x + box->width);
   } else {
      if (usage & PIPE_MAP_WRITE && !(usage & PIPE_MAP_READ))
         /* this is like a blit, so we can potentially dump some clears or maybe we have to  */
         zink_fb_clears_apply_or_discard(ctx, pres, zink_rect_from_box(box), false);
      else if (usage & PIPE_MAP_READ)
         /* if the map region intersects with any clears then we have to apply them */
         zink_fb_clears_apply_region(ctx, pres, zink_rect_from_box(box));
      if (res->optimal_tiling || !res->host_visible) {
         enum pipe_format format = pres->format;
         if (usage & PIPE_MAP_DEPTH_ONLY)
            format = util_format_get_depth_only(pres->format);
         else if (usage & PIPE_MAP_STENCIL_ONLY)
            format = PIPE_FORMAT_S8_UINT;
         trans->base.stride = util_format_get_stride(format, box->width);
         trans->base.layer_stride = util_format_get_2d_size(format,
                                                            trans->base.stride,
                                                            box->height);

         struct pipe_resource templ = *pres;
         templ.format = format;
         templ.usage = PIPE_USAGE_STAGING;
         templ.target = PIPE_BUFFER;
         templ.bind = 0;
         templ.width0 = trans->base.layer_stride * box->depth;
         templ.height0 = templ.depth0 = 0;
         templ.last_level = 0;
         templ.array_size = 1;
         templ.flags = 0;

         trans->staging_res = zink_resource_create(pctx->screen, &templ);
         if (!trans->staging_res)
            return NULL;

         struct zink_resource *staging_res = zink_resource(trans->staging_res);

         if (usage & PIPE_MAP_READ) {
            /* TODO: can probably just do a full cs copy if it's already in a cs batch */
            if (batch_uses & (ZINK_RESOURCE_ACCESS_WRITE << ZINK_COMPUTE_BATCH_ID))
               /* don't actually have to stall here, only ensure batch is submitted */
               zink_flush_compute(ctx);
            struct zink_context *ctx = zink_context(pctx);
            bool ret = zink_transfer_copy_bufimage(ctx, res,
                                                   staging_res, trans,
                                                   false);
            if (ret == false)
               return NULL;

            /* need to wait for rendering to finish */
            zink_fence_wait(pctx);
         }

         VkResult result = vkMapMemory(screen->dev, staging_res->mem,
                                       staging_res->offset,
                                       staging_res->size, 0, &ptr);
         if (result != VK_SUCCESS)
            return NULL;

      } else {
         assert(!res->optimal_tiling);
         /* special case compute reads since they aren't handled by zink_fence_wait() */
         if (batch_uses & (ZINK_RESOURCE_ACCESS_READ << ZINK_COMPUTE_BATCH_ID))
            zink_wait_on_batch(ctx, ZINK_COMPUTE_BATCH_ID);
         batch_uses &= ~(ZINK_RESOURCE_ACCESS_READ << ZINK_COMPUTE_BATCH_ID);
         if (batch_uses >= ZINK_RESOURCE_ACCESS_WRITE) {
            if (usage & PIPE_MAP_READ)
               resource_sync_writes_from_batch_id(ctx, batch_uses, zink_curr_batch(ctx)->batch_id);
            else
               zink_fence_wait(pctx);
         }
         VkResult result = vkMapMemory(screen->dev, res->mem, res->offset, res->size, 0, &ptr);
         if (result != VK_SUCCESS)
            return NULL;
         VkImageSubresource isr = {
            res->aspect,
            level,
            0
         };
         VkSubresourceLayout srl;
         vkGetImageSubresourceLayout(screen->dev, res->image, &isr, &srl);
         trans->base.stride = srl.rowPitch;
         trans->base.layer_stride = srl.arrayPitch;
         const struct util_format_description *desc = util_format_description(res->base.format);
         unsigned offset = srl.offset +
                           box->z * srl.depthPitch +
                           (box->y / desc->block.height) * srl.rowPitch +
                           (box->x / desc->block.width) * (desc->block.bits / 8);
         ptr = ((uint8_t *)ptr) + offset;
      }
   }
   if ((usage & PIPE_MAP_PERSISTENT) && !(usage & PIPE_MAP_COHERENT))
      res->persistent_maps++;

   *transfer = &trans->base;
   return ptr;
}

static void
zink_transfer_flush_region(struct pipe_context *pctx,
                           struct pipe_transfer *ptrans,
                           const struct pipe_box *box)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(ptrans->resource);
   struct zink_transfer *trans = (struct zink_transfer *)ptrans;

   if (trans->base.usage & PIPE_MAP_WRITE) {
      if (trans->staging_res) {
         struct zink_resource *staging_res = zink_resource(trans->staging_res);
         uint32_t batch_uses = zink_get_resource_usage(res) | zink_get_resource_usage(staging_res);
         if (batch_uses & (ZINK_RESOURCE_ACCESS_WRITE << ZINK_COMPUTE_BATCH_ID)) {
            /* don't actually have to stall here, only ensure batch is submitted */
            zink_flush_compute(ctx);
            batch_uses &= ~(ZINK_RESOURCE_ACCESS_WRITE << ZINK_COMPUTE_BATCH_ID);
            batch_uses &= ~(ZINK_RESOURCE_ACCESS_READ << ZINK_COMPUTE_BATCH_ID);
         }

         if (ptrans->resource->target == PIPE_BUFFER)
            zink_copy_buffer(ctx, NULL, res, staging_res, box->x, box->x, box->width);
         else
            zink_transfer_copy_bufimage(ctx, res, staging_res, trans, true);
         if (batch_uses)
            pctx->flush(pctx, NULL, 0);
      }
   }
}

static void
zink_transfer_unmap(struct pipe_context *pctx,
                    struct pipe_transfer *ptrans)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(ptrans->resource);
   struct zink_transfer *trans = (struct zink_transfer *)ptrans;
   if (trans->staging_res) {
      struct zink_resource *staging_res = zink_resource(trans->staging_res);
      vkUnmapMemory(screen->dev, staging_res->mem);
   } else
      vkUnmapMemory(screen->dev, res->mem);
   if ((trans->base.usage & PIPE_MAP_PERSISTENT) && !(trans->base.usage & PIPE_MAP_COHERENT))
      res->persistent_maps--;
   if (!(trans->base.usage & (PIPE_MAP_FLUSH_EXPLICIT | PIPE_MAP_COHERENT))) {
      zink_transfer_flush_region(pctx, ptrans, &ptrans->box);
   }

   if (trans->staging_res)
      pipe_resource_reference(&trans->staging_res, NULL);
   pipe_resource_reference(&trans->base.resource, NULL);
   slab_free(&ctx->transfer_pool, ptrans);
}

static struct pipe_resource *
zink_resource_get_separate_stencil(struct pipe_resource *pres)
{
   /* For packed depth-stencil, we treat depth as the primary resource
    * and store S8 as the "second plane" resource.
    */
   if (pres->next && pres->next->format == PIPE_FORMAT_S8_UINT)
      return pres->next;

   return NULL;

}

void
zink_resource_setup_transfer_layouts(struct zink_context *ctx, struct zink_resource *src, struct zink_resource *dst)
{
   if (src == dst) {
      /* The Vulkan 1.1 specification says the following about valid usage
       * of vkCmdBlitImage:
       *
       * "srcImageLayout must be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR,
       *  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL or VK_IMAGE_LAYOUT_GENERAL"
       *
       * and:
       *
       * "dstImageLayout must be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR,
       *  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL or VK_IMAGE_LAYOUT_GENERAL"
       *
       * Since we cant have the same image in two states at the same time,
       * we're effectively left with VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR or
       * VK_IMAGE_LAYOUT_GENERAL. And since this isn't a present-related
       * operation, VK_IMAGE_LAYOUT_GENERAL seems most appropriate.
       */
      zink_resource_image_barrier(ctx, NULL, src,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);
   } else {
      zink_resource_image_barrier(ctx, NULL, src,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_ACCESS_TRANSFER_READ_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);

      zink_resource_image_barrier(ctx, NULL, dst,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);
   }
}

void
zink_get_depth_stencil_resources(struct pipe_resource *res,
                                 struct zink_resource **out_z,
                                 struct zink_resource **out_s)
{
   if (!res) {
      if (out_z) *out_z = NULL;
      if (out_s) *out_s = NULL;
      return;
   }

   if (res->format != PIPE_FORMAT_S8_UINT) {
      if (out_z) *out_z = zink_resource(res);
      if (out_s) *out_s = zink_resource(zink_resource_get_separate_stencil(res));
   } else {
      if (out_z) *out_z = NULL;
      if (out_s) *out_s = zink_resource(res);
   }
}

static void
zink_resource_set_separate_stencil(struct pipe_resource *pres,
                                   struct pipe_resource *stencil)
{
   assert(util_format_has_depth(util_format_description(pres->format)));
   pipe_resource_reference(&pres->next, stencil);
}

static enum pipe_format
zink_resource_get_internal_format(struct pipe_resource *pres)
{
   struct zink_resource *res = zink_resource(pres);
   return res->internal_format;
}

static const struct u_transfer_vtbl transfer_vtbl = {
   .resource_create       = zink_resource_create,
   .resource_destroy      = zink_resource_destroy,
   .transfer_map          = zink_transfer_map,
   .transfer_unmap        = zink_transfer_unmap,
   .transfer_flush_region = zink_transfer_flush_region,
   .get_internal_format   = zink_resource_get_internal_format,
   .set_stencil           = zink_resource_set_separate_stencil,
   .get_stencil           = zink_resource_get_separate_stencil,
};

bool
zink_screen_resource_init(struct pipe_screen *pscreen)
{
   struct zink_screen *screen = zink_screen(pscreen);
   pscreen->resource_create = zink_resource_create;
   pscreen->resource_destroy = zink_resource_destroy;
   pscreen->transfer_helper = u_transfer_helper_create(&transfer_vtbl, true, true, false, false);

   if (screen->info.have_KHR_external_memory_fd) {
      pscreen->resource_get_handle = zink_resource_get_handle;
      pscreen->resource_from_handle = zink_resource_from_handle;
   }
   simple_mtx_init(&screen->mem_cache_mtx, mtx_plain);
   screen->resource_mem_cache = _mesa_hash_table_create(NULL, mem_hash, mem_equals);
   return !!screen->resource_mem_cache;
}

void
zink_context_resource_init(struct pipe_context *pctx)
{
   pctx->transfer_map = u_transfer_helper_deinterleave_transfer_map;
   pctx->transfer_unmap = u_transfer_helper_deinterleave_transfer_unmap;

   pctx->transfer_flush_region = u_transfer_helper_transfer_flush_region;
   pctx->buffer_subdata = u_default_buffer_subdata;
   pctx->texture_subdata = u_default_texture_subdata;
}
