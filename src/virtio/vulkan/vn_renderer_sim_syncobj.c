/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vn_renderer_sim_syncobj.h"

#include <errno.h>
#include <poll.h>
#include <unistd.h>

#include "util/hash_table.h"
#include "util/os_file.h"
#include "util/u_idalloc.h"

#include "vn_renderer.h"

static struct {
   mtx_t mutex;
   struct hash_table *syncobjs;
   struct util_idalloc ida;
} sim;

struct sim_syncobj {
   mtx_t mutex;
   uint64_t point;

   int pending_fd;
   uint64_t pending_point;
};

static void
sim_syncobj_init_once(void)
{
   mtx_init(&sim.mutex, mtx_plain);
}

uint32_t
sim_syncobj_create(bool signaled)
{
   static once_flag once = ONCE_FLAG_INIT;
   call_once(&once, sim_syncobj_init_once);

   struct sim_syncobj *syncobj = calloc(1, sizeof(*syncobj));
   if (!syncobj)
      return 0;

   mtx_init(&syncobj->mutex, mtx_plain);
   syncobj->pending_fd = -1;
   if (signaled)
      syncobj->point = syncobj->pending_point = 1;

   mtx_lock(&sim.mutex);

   /* initialize lazily */
   if (!sim.syncobjs) {
      sim.syncobjs = _mesa_pointer_hash_table_create(NULL);
      if (!sim.syncobjs) {
         mtx_unlock(&sim.mutex);
         mtx_destroy(&syncobj->mutex);
         free(syncobj);
         return 0;
      }

      util_idalloc_init(&sim.ida, 32);
   }

   const unsigned syncobj_handle = util_idalloc_alloc(&sim.ida) + 1;
   _mesa_hash_table_insert(sim.syncobjs,
                           (const void *)(uintptr_t)syncobj_handle, syncobj);

   mtx_unlock(&sim.mutex);

   return syncobj_handle;
}

void
sim_syncobj_destroy(uint32_t syncobj_handle)
{
   struct sim_syncobj *syncobj = NULL;

   mtx_lock(&sim.mutex);

   struct hash_entry *entry = _mesa_hash_table_search(
      sim.syncobjs, (const void *)(uintptr_t)syncobj_handle);
   if (entry) {
      syncobj = entry->data;
      _mesa_hash_table_remove(sim.syncobjs, entry);
      util_idalloc_free(&sim.ida, syncobj_handle - 1);
   }

   mtx_unlock(&sim.mutex);

   if (syncobj) {
      if (syncobj->pending_fd >= 0)
         close(syncobj->pending_fd);
      mtx_destroy(&syncobj->mutex);
      free(syncobj);
   }
}

static bool
sim_syncobj_poll(int fd, int poll_timeout)
{
   struct pollfd pollfd = {
      .fd = fd,
      .events = POLLIN,
   };
   int ret;
   do {
      ret = poll(&pollfd, 1, poll_timeout);
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

   return ret > 0 && (pollfd.revents & POLLIN);
}

static void
sim_syncobj_set_point_locked(struct sim_syncobj *syncobj, uint64_t point)
{
   syncobj->point = point;

   if (syncobj->pending_fd >= 0) {
      close(syncobj->pending_fd);
      syncobj->pending_fd = -1;
      syncobj->pending_point = point;
   }
}

static void
sim_syncobj_update_point_locked(struct sim_syncobj *syncobj, int poll_timeout)
{
   if (syncobj->pending_fd >= 0) {
      if (sim_syncobj_poll(syncobj->pending_fd, poll_timeout)) {
         close(syncobj->pending_fd);
         syncobj->pending_fd = -1;
         syncobj->point = syncobj->pending_point;
      }
   }
}

static struct sim_syncobj *
sim_syncobj_lookup(uint32_t syncobj_handle)
{
   struct sim_syncobj *syncobj = NULL;

   mtx_lock(&sim.mutex);
   struct hash_entry *entry = _mesa_hash_table_search(
      sim.syncobjs, (const void *)(uintptr_t)syncobj_handle);
   if (entry)
      syncobj = entry->data;
   mtx_unlock(&sim.mutex);

   return syncobj;
}

int
sim_syncobj_reset(uint32_t syncobj_handle)
{
   struct sim_syncobj *syncobj = sim_syncobj_lookup(syncobj_handle);
   if (!syncobj)
      return -1;

   mtx_lock(&syncobj->mutex);
   sim_syncobj_set_point_locked(syncobj, 0);
   mtx_unlock(&syncobj->mutex);

   return 0;
}

int
sim_syncobj_query(uint32_t syncobj_handle, uint64_t *point)
{
   struct sim_syncobj *syncobj = sim_syncobj_lookup(syncobj_handle);
   if (!syncobj)
      return -1;

   mtx_lock(&syncobj->mutex);
   sim_syncobj_update_point_locked(syncobj, 0);
   *point = syncobj->point;
   mtx_unlock(&syncobj->mutex);

   return 0;
}

int
sim_syncobj_signal(uint32_t syncobj_handle, uint64_t point)
{
   struct sim_syncobj *syncobj = sim_syncobj_lookup(syncobj_handle);
   if (!syncobj)
      return -1;

   mtx_lock(&syncobj->mutex);
   sim_syncobj_set_point_locked(syncobj, point);
   mtx_unlock(&syncobj->mutex);

   return 0;
}

int
sim_syncobj_submit(uint32_t syncobj_handle, int sync_fd, uint64_t point)
{
   struct sim_syncobj *syncobj = sim_syncobj_lookup(syncobj_handle);
   if (!syncobj)
      return -1;

   int pending_fd = os_dupfd_cloexec(sync_fd);
   if (pending_fd < 0) {
      vn_log(NULL, "failed to dup sync fd");
      return -1;
   }

   mtx_lock(&syncobj->mutex);

   if (syncobj->pending_fd >= 0) {
      mtx_unlock(&syncobj->mutex);

      /* TODO */
      vn_log(NULL, "sorry, no simulated timeline semaphore");
      close(pending_fd);
      return -1;
   }
   if (syncobj->point >= point)
      vn_log(NULL, "non-monotonic signaling");

   syncobj->pending_fd = pending_fd;
   syncobj->pending_point = point;

   mtx_unlock(&syncobj->mutex);

   return 0;
}

static int
timeout_to_poll_timeout(uint64_t timeout)
{
   const uint64_t ns_per_ms = 1000000;
   const uint64_t ms = (timeout + ns_per_ms - 1) / ns_per_ms;
   if (!ms && timeout)
      return -1;
   return ms <= INT_MAX ? ms : -1;
}

int
sim_syncobj_wait(const struct vn_renderer_wait *wait)
{
   const int poll_timeout = timeout_to_poll_timeout(wait->timeout);

   /* TODO poll all fds at the same time */
   for (uint32_t i = 0; i < wait->sync_count; i++) {
      const uint64_t point = wait->sync_values[i];

      struct sim_syncobj *syncobj =
         sim_syncobj_lookup(wait->syncs[i]->syncobj_handle);
      if (!syncobj)
         return -1;

      mtx_lock(&syncobj->mutex);

      if (syncobj->point < point)
         sim_syncobj_update_point_locked(syncobj, poll_timeout);

      if (syncobj->point < point) {
         if (wait->wait_any && i < wait->sync_count - 1 &&
             syncobj->pending_fd < 0) {
            mtx_unlock(&syncobj->mutex);
            continue;
         }
         errno = ETIME;
         mtx_unlock(&syncobj->mutex);
         return -1;
      }

      mtx_unlock(&syncobj->mutex);

      if (wait->wait_any)
         break;

      /* TODO adjust poll_timeout */
   }

   return 0;
}

int
sim_syncobj_export(uint32_t syncobj_handle)
{
   struct sim_syncobj *syncobj = sim_syncobj_lookup(syncobj_handle);
   if (!syncobj)
      return -1;

   int fd = -1;
   mtx_lock(&syncobj->mutex);
   if (syncobj->pending_fd >= 0)
      fd = os_dupfd_cloexec(syncobj->pending_fd);
   else
      fd = -1;
   mtx_unlock(&syncobj->mutex);

   return fd;
}

uint32_t
sim_syncobj_import(uint32_t syncobj_handle, int fd)
{
   struct sim_syncobj *syncobj = sim_syncobj_lookup(syncobj_handle);
   if (!syncobj)
      return 0;

   if (sim_syncobj_submit(syncobj_handle, fd, 1))
      return 0;

   return syncobj_handle;
}
