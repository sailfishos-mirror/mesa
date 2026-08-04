/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <dlfcn.h>
#include <new>
#include "perf/intel_perf_metrics_library.h"
#include "metrics_library/metrics_library_api_1_0.h"

#include "perf/intel_perf.h"

using namespace MetricsLibraryApi;

static ClientGen get_client_gen_from_devinfo(const struct intel_device_info *devinfo)
{
   switch (devinfo->verx10) {
   case 90:
      return ClientGen::Gen9;
   case 110:
      return ClientGen::Gen11;
   case 120:
      return ClientGen::Gen12;
   case 125:
      return ClientGen::XeHPG;
   case 200:
      return ClientGen::Xe2HPG;
   case 300:
      return ClientGen::Xe3;
   default:
      return ClientGen::Unknown;
   }
}

bool intel_perf_init_metrics_library(struct intel_perf_config *perf, int fd)
{
   const char *libstr = "libigdml.so.1";
   void *metrics_library = dlopen(libstr, RTLD_NOW | RTLD_LOCAL);

   if (!metrics_library)
      return false;

   ContextCreateFunction_1_0 create_context = (ContextCreateFunction_1_0)dlsym(metrics_library, METRICS_LIBRARY_CONTEXT_CREATE_1_0);
   ContextDeleteFunction_1_0 destroy_context = (ContextDeleteFunction_1_0)dlsym(metrics_library, METRICS_LIBRARY_CONTEXT_DELETE_1_0);

   if (!create_context || !destroy_context) {
      dlclose(metrics_library);
      return false;
   }

   StatusCode status;
   ContextHandle_1_0 context_handle;
   ClientType_1_0 client_type = {};
   ClientData_1_0 client_data = {};
   ClientDataLinuxAdapter_1_0 adapter_data = {};
   ContextCreateData_1_0 create_data = {};
   ClientOptionsData_1_0 client_options[3] = {};

   client_options[0].Type = ClientOptionsType::SubDevice;
   client_options[0].SubDevice.Enabled = false;
   client_options[1].Type = ClientOptionsType::SubDeviceIndex;
   client_options[1].SubDeviceIndex.Index = 0;
   client_options[2].Type = ClientOptionsType::SubDeviceCount;
   client_options[2].SubDeviceCount.Count = 1;

   client_type.Gen = get_client_gen_from_devinfo(perf->devinfo);
   client_type.Api = ClientApi::Vulkan;

   client_data.Linux.Adapter = &adapter_data;
   client_data.Linux.Adapter->Type = LinuxAdapterType::DrmFileDescriptor;
   client_data.Linux.Adapter->DrmFileDescriptor = fd;
   client_data.ClientOptions = client_options;
   client_data.ClientOptionsCount = sizeof(client_options) / sizeof(client_options[0]);

   perf->metrics_library.api = new (std::nothrow) Interface_1_0();
   perf->metrics_library.callbacks = new (std::nothrow) ClientCallbacks_1_0();

   create_data.Api = (Interface_1_0*)perf->metrics_library.api;
   create_data.ClientCallbacks = (ClientCallbacks_1_0*)perf->metrics_library.callbacks;
   create_data.ClientData = &client_data;

   status = create_context(client_type, &create_data, &context_handle);
   if (status != StatusCode::Success || !context_handle.data) {
      delete (Interface_1_0*)perf->metrics_library.api;
      perf->metrics_library.api = nullptr;
      delete (ClientCallbacks_1_0*)perf->metrics_library.callbacks;
      perf->metrics_library.callbacks = nullptr;
      dlclose(metrics_library);
      return false;
   }

   perf->metrics_library.lib = metrics_library;
   perf->metrics_library.context = (void*)context_handle.data;
   perf->metrics_library.destroy_context_func = (void*)destroy_context;

   return true;
}

bool intel_perf_deinit_metrics_library(struct intel_perf_config *perf)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.destroy_context_func)
      return false;

   ContextHandle_1_0 context_handle = {};
   context_handle.data = perf->metrics_library.context;

   ContextDeleteFunction_1_0 destroy_context = (ContextDeleteFunction_1_0)perf->metrics_library.destroy_context_func;
   StatusCode status = destroy_context(context_handle);
   if (status != StatusCode::Success)
      return false;

   delete (Interface_1_0*)perf->metrics_library.api;
   perf->metrics_library.api = nullptr;
   delete (ClientCallbacks_1_0*)perf->metrics_library.callbacks;
   perf->metrics_library.callbacks = nullptr;
   dlclose(perf->metrics_library.lib);
   perf->metrics_library.lib = nullptr;
   perf->metrics_library.context = nullptr;
   perf->metrics_library.destroy_context_func = nullptr;

   return true;
}
