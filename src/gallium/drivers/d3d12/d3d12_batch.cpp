/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "d3d12_batch.h"
#include "d3d12_context.h"
#include "d3d12_fence.h"
#include "d3d12_query.h"
#include "d3d12_residency.h"
#include "d3d12_resource.h"
#include "d3d12_resource_state.h"
#include "d3d12_screen.h"
#include "d3d12_surface.h"

#include "util/hash_table.h"
#include "util/set.h"
#include "util/u_inlines.h"

#include <dxguids/dxguids.h>


unsigned d3d12_sampler_desc_table_key_hash(const void* key)
{
   const d3d12_sampler_desc_table_key* table = (d3d12_sampler_desc_table_key*)key;

   return _mesa_hash_data(table->descs, sizeof(table->descs[0]) * table->count);
}
bool d3d12_sampler_desc_table_key_equals(const void* a, const void* b)
{
   const d3d12_sampler_desc_table_key* table_a = (d3d12_sampler_desc_table_key*)a;
   const d3d12_sampler_desc_table_key* table_b = (d3d12_sampler_desc_table_key*)b;
   return table_a->count == table_b->count && memcmp(table_a->descs, table_b->descs, sizeof(table_a->descs[0]) * table_a->count) == 0;
}

bool
d3d12_init_batch(struct d3d12_context *ctx, struct d3d12_batch *batch)
{
   struct d3d12_screen *screen = d3d12_screen(ctx->base.screen);

   batch->bos = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                        _mesa_key_pointer_equal);

   util_dynarray_init(&batch->local_bos, NULL);

   batch->surfaces = _mesa_set_create(NULL, _mesa_hash_pointer,
                                      _mesa_key_pointer_equal);
   batch->objects = _mesa_set_create(NULL,
                                     _mesa_hash_pointer,
                                     _mesa_key_pointer_equal);

   if (!batch->bos || !batch->surfaces || !batch->objects)
      return false;

#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   if (screen->max_feature_level >= D3D_FEATURE_LEVEL_11_0) {
      batch->queries = _mesa_set_create(NULL, _mesa_hash_pointer,
                                        _mesa_key_pointer_equal);

      batch->view_heap =
         d3d12_descriptor_heap_new(screen->dev,
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                    D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                                    8096);

      batch->sampler_tables = _mesa_hash_table_create(NULL, d3d12_sampler_desc_table_key_hash,
                                                      d3d12_sampler_desc_table_key_equals);
      batch->sampler_views = _mesa_set_create(NULL, _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);

      if (!batch->sampler_tables || !batch->sampler_views || !batch->view_heap || !batch->queries)
         return false;

      util_dynarray_init(&batch->zombie_samplers, NULL);

      batch->sampler_heap =
         d3d12_descriptor_heap_new(screen->dev,
                                 D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                 D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                                 1024);

      if (!batch->sampler_heap)
         return false;
   }
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   if (FAILED(screen->dev->CreateCommandAllocator(screen->queue_type,
                                                  IID_PPV_ARGS(&batch->cmdalloc))))
      return false;

   return true;
}

static inline void
delete_bo(d3d12_bo *bo)
{
   d3d12_bo_unreference(bo);
}
static void
delete_bo_entry(hash_entry *entry)
{
   struct d3d12_bo *bo = (struct d3d12_bo *)entry->key;
   d3d12_bo_unreference(bo);
}

static void
delete_sampler_view_table(hash_entry *entry)
{
   FREE((void*)entry->key);
   FREE(entry->data);
}

static void
delete_sampler_view(set_entry *entry)
{
   struct pipe_sampler_view *pres = (struct pipe_sampler_view *)entry->key;
   pipe_sampler_view_reference(&pres, NULL);
}

static void
delete_surface(set_entry *entry)
{
   struct pipe_surface *surf = (struct pipe_surface *)entry->key;
   pipe_surface_reference(&surf, NULL);
}

static void
delete_object(set_entry *entry)
{
   ID3D12Object *object = (ID3D12Object *)entry->key;
   object->Release();
}

#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
static void
delete_query(set_entry *entry)
{
   struct d3d12_query *query = (struct d3d12_query *)entry->key;
   if (pipe_reference(&query->reference, nullptr))
      d3d12_destroy_query(query);
}
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

bool
d3d12_reset_batch(struct d3d12_context *ctx, struct d3d12_batch *batch, uint64_t timeout_ns)
{
   // batch hasn't been submitted before
   if (!batch->fence && !batch->has_errors)
      return true;

   if (batch->fence) {
      if (!d3d12_fence_finish(batch->fence, timeout_ns))
         return false;
      d3d12_fence_reference(&batch->fence, NULL);
   }

   _mesa_hash_table_clear(batch->bos, delete_bo_entry);
   _mesa_set_clear(batch->surfaces, delete_surface);
   _mesa_set_clear(batch->objects, delete_object);
   
   util_dynarray_foreach(&batch->local_bos, d3d12_bo*, bo) {
      (*bo)->local_reference_mask[batch->ctx_id] &= ~(1 << batch->ctx_index);
      delete_bo(*bo);
   }
   util_dynarray_clear(&batch->local_bos);

#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   if (d3d12_screen(ctx->base.screen)->max_feature_level >= D3D_FEATURE_LEVEL_11_0) {
      _mesa_hash_table_clear(batch->sampler_tables, delete_sampler_view_table);
      _mesa_set_clear(batch->sampler_views, delete_sampler_view);

      _mesa_set_clear(batch->queries, delete_query);
      util_dynarray_foreach(&batch->zombie_samplers, d3d12_descriptor_handle, handle)
         d3d12_descriptor_handle_free(handle);
      util_dynarray_clear(&batch->zombie_samplers);
      d3d12_descriptor_heap_clear(batch->view_heap);
      d3d12_descriptor_heap_clear(batch->sampler_heap);
   }
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   if (FAILED(batch->cmdalloc->Reset())) {
      debug_printf("D3D12: resetting ID3D12CommandAllocator failed\n");
      return false;
   }
   batch->has_errors = false;
   batch->pending_memory_barrier = false;
   return true;
}

void
d3d12_destroy_batch(struct d3d12_context *ctx, struct d3d12_batch *batch)
{
   d3d12_reset_batch(ctx, batch, OS_TIMEOUT_INFINITE);
   batch->cmdalloc->Release();
   _mesa_hash_table_destroy(batch->bos, NULL);

#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   if (d3d12_screen(ctx->base.screen)->max_feature_level >= D3D_FEATURE_LEVEL_11_0) {
      d3d12_descriptor_heap_free(batch->sampler_heap);
      d3d12_descriptor_heap_free(batch->view_heap);
      _mesa_hash_table_destroy(batch->sampler_tables, NULL);
      _mesa_set_destroy(batch->sampler_views, NULL);
      _mesa_set_destroy(batch->queries, NULL);
      util_dynarray_fini(&batch->zombie_samplers);
   }
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   _mesa_set_destroy(batch->surfaces, NULL);
   _mesa_set_destroy(batch->objects, NULL);
   util_dynarray_fini(&batch->local_bos);
}

void
d3d12_start_batch(struct d3d12_context *ctx, struct d3d12_batch *batch)
{
   struct d3d12_screen *screen = d3d12_screen(ctx->base.screen);
   d3d12_reset_batch(ctx, batch, OS_TIMEOUT_INFINITE);

   /* Create or reset global command list */
   if (ctx->cmdlist) {
      if (FAILED(ctx->cmdlist->Reset(batch->cmdalloc, NULL))) {
         debug_printf("D3D12: resetting ID3D12GraphicsCommandList failed\n");
         batch->has_errors = true;
         return;
      }
   } else {
      if (FAILED(screen->dev->CreateCommandList(0, screen->queue_type,
                                                batch->cmdalloc, NULL,
                                                IID_PPV_ARGS(&ctx->cmdlist)))) {
         debug_printf("D3D12: creating ID3D12GraphicsCommandList failed\n");
         batch->has_errors = true;
         return;
      }
      if (FAILED(ctx->cmdlist->QueryInterface(IID_PPV_ARGS(&ctx->cmdlist2)))) {
         ctx->cmdlist2 = nullptr;
      }
      if (FAILED(ctx->cmdlist->QueryInterface(IID_PPV_ARGS(&ctx->cmdlist8)))) {
         ctx->cmdlist8 = nullptr;
      }
   }

#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   if (screen->max_feature_level >= D3D_FEATURE_LEVEL_11_0) {
      ID3D12DescriptorHeap* heaps[2] = { d3d12_descriptor_heap_get(batch->view_heap),
                                       d3d12_descriptor_heap_get(batch->sampler_heap) };
      ctx->cmdlist->SetDescriptorHeaps(2, heaps);

      ctx->cmdlist_dirty = ~0;
      for (int i = 0; i < PIPE_SHADER_TYPES; ++i)
         ctx->shader_dirty[i] = ~0;
   
      if (!ctx->queries_disabled)
         d3d12_resume_queries(ctx);
      if (ctx->current_predication)
         d3d12_enable_predication(ctx);
   }
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   batch->submit_id = ++ctx->submit_id;
}

void
d3d12_end_batch(struct d3d12_context *ctx, struct d3d12_batch *batch)
{
   struct d3d12_screen *screen = d3d12_screen(ctx->base.screen);

#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   if (!ctx->queries_disabled)
      d3d12_suspend_queries(ctx);
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   if (FAILED(ctx->cmdlist->Close())) {
      debug_printf("D3D12: closing ID3D12GraphicsCommandList failed\n");
      batch->has_errors = true;
      return;
   }

   mtx_lock(&screen->submit_mutex);

#ifndef _GAMING_XBOX
   d3d12_process_batch_residency(screen, batch);
#endif

   bool has_state_fixup = d3d12_context_state_resolve_submission(ctx, batch);

   ID3D12CommandList *cmdlists[] = { ctx->state_fixup_cmdlist, ctx->cmdlist };
   ID3D12CommandList **to_execute = cmdlists;
   UINT count_to_execute = ARRAY_SIZE(cmdlists);
   if (!has_state_fixup) {
      to_execute++;
      count_to_execute--;
   }
   screen->cmdqueue->ExecuteCommandLists(count_to_execute, to_execute);

   batch->fence = d3d12_create_fence(screen, true);

#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   /* batch->queries is NULL when no grfx supported */
   if (screen->max_feature_level >= D3D_FEATURE_LEVEL_11_0) {
      set_foreach_remove(batch->queries, entry) {
         d3d12_query *query = (struct d3d12_query *)entry->key;
         if (pipe_reference(&query->reference, nullptr))
            d3d12_destroy_query(query);
         else
            query->fence_value = screen->fence_value;
      }
   }
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   mtx_unlock(&screen->submit_mutex);
}


inline uint8_t*
d3d12_batch_get_reference(struct d3d12_batch *batch,
                          struct d3d12_bo *bo)
{
   if (batch->ctx_id != D3D12_CONTEXT_NO_ID) {
      if ((bo->local_reference_mask[batch->ctx_id] & (1 << batch->ctx_index)) != 0) {
         return &bo->local_reference_state[batch->ctx_id][batch->ctx_index];
      }
      else
         return NULL;
   }
   else {
      hash_entry* entry = _mesa_hash_table_search(batch->bos, bo);
      if (entry == NULL)
         return NULL;
      else
         return (uint8_t*)&entry->data;
   }
}

inline uint8_t*
d3d12_batch_acquire_reference(struct d3d12_batch *batch,
                          struct d3d12_bo *bo)
{
   if (batch->ctx_id != D3D12_CONTEXT_NO_ID) {
      if ((bo->local_reference_mask[batch->ctx_id] & (1 << batch->ctx_index)) == 0) {
         d3d12_bo_reference(bo);
         util_dynarray_append(&batch->local_bos, d3d12_bo*, bo);
         bo->local_reference_mask[batch->ctx_id] |= (1 << batch->ctx_index);
         bo->local_reference_state[batch->ctx_id][batch->ctx_index] = batch_bo_reference_none;
      }
      return &bo->local_reference_state[batch->ctx_id][batch->ctx_index];
   }
   else {
      hash_entry* entry = _mesa_hash_table_search(batch->bos, bo);
      if (entry == NULL) {
         d3d12_bo_reference(bo);
         entry = _mesa_hash_table_insert(batch->bos, bo, NULL);
      }

      return (uint8_t*)&entry->data;
   }
}

bool
d3d12_batch_has_references(struct d3d12_batch *batch,
                           struct d3d12_bo *bo,
                           bool want_to_write)
{
   uint8_t*state = d3d12_batch_get_reference(batch, bo);
   if (state == NULL)
      return false;
   bool resource_was_written = ((batch_bo_reference_state)(size_t)*state & batch_bo_reference_written) != 0;
   return want_to_write || resource_was_written;
}

void
d3d12_batch_reference_resource(struct d3d12_batch *batch,
                               struct d3d12_resource *res,
                               bool write)
{
   uint8_t*state = d3d12_batch_acquire_reference(batch, res->bo);

   uint8_t new_data = write ? batch_bo_reference_written : batch_bo_reference_read;
   uint8_t old_data = (uint8_t)*state;
   *state = (old_data | new_data);
}

void
d3d12_batch_reference_sampler_view(struct d3d12_batch *batch,
                                   struct d3d12_sampler_view *sv)
{
   struct set_entry *entry = _mesa_set_search(batch->sampler_views, sv);
   if (!entry) {
      entry = _mesa_set_add(batch->sampler_views, sv);
      pipe_reference(NULL, &sv->base.reference);

      d3d12_batch_reference_resource(batch, d3d12_resource(sv->base.texture), false);
   }
}

void
d3d12_batch_reference_surface_texture(struct d3d12_batch *batch,
                                      struct d3d12_surface *surf)
{
   d3d12_batch_reference_resource(batch, d3d12_resource(surf->base.texture), true);
}

void
d3d12_batch_reference_object(struct d3d12_batch *batch,
                             ID3D12Object *object)
{
   struct set_entry *entry = _mesa_set_search(batch->objects, object);
   if (!entry) {
      entry = _mesa_set_add(batch->objects, object);
      object->AddRef();
   }
}

void
d3d12_batch_reference_query(struct d3d12_batch *batch,
                            struct d3d12_query *query)
{
   struct set_entry *entry = _mesa_set_search(batch->queries, query);
   if (!entry) {
      entry = _mesa_set_add(batch->queries, query);
      pipe_reference(NULL, &query->reference);
   }
}
