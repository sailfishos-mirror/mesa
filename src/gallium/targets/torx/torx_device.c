/*
 * Copyright (c) 2025 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "pipe/p_state.h"

#include "torx_device.h"

#ifdef GALLIUM_ETHOSU
#include "ethosu/ethosu_public.h"
#endif

struct ml_driver_descriptor {
   const char *driver_name;
   struct pipe_ml_device *(*create_device)(const char *spec);
};

static const struct ml_driver_descriptor driver_descriptors[] = {
#ifdef GALLIUM_ETHOSU
   {.driver_name = "ethosu", .create_device = ethosu_ml_device_create},
#endif
};

const struct pipe_ml_device *
torx_device_probe(void)
{
   const char *env = getenv("MESA_ML_DEVICE"); /* DRIVER-GEN-MACS-SRAM, eg. "ethosu-65-256-98304" */
   char *driver_name = NULL;
   const char *spec = NULL;
   const struct ml_driver_descriptor *dd = NULL;
   struct pipe_ml_device *ml_device = NULL;

   if (env == NULL)
      return NULL;

   spec = strchr(env, '-');
   if (!spec)
      return NULL;

   size_t len = spec - env;
   driver_name = strndup(env, len);
   spec++; // skip '-'

   if (driver_name) {
      for (int i = 0; i < ARRAY_SIZE(driver_descriptors); i++) {
         if (strcmp(driver_descriptors[i].driver_name, driver_name) == 0) {
            dd = &driver_descriptors[i];

            ml_device = dd->create_device(spec);
            if (ml_device == NULL) {
               fprintf(stderr, "Failed to create ML device for spec '%s' using driver '%s'\n", spec, driver_name);
               free(driver_name);
               return NULL;
            }

            ml_device->id = env;
            break;
         }
      }
   }

   free(driver_name);

   return ml_device;
}
