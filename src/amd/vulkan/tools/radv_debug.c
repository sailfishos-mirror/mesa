/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_debug.h"

uint32_t
radv_find_memory_index(const struct radv_physical_device *pdev, VkMemoryPropertyFlags flags)
{
   const VkPhysicalDeviceMemoryProperties *mem_properties = &pdev->memory_properties;
   for (uint32_t i = 0; i < mem_properties->memoryTypeCount; ++i) {
      if (mem_properties->memoryTypes[i].propertyFlags == flags) {
         return i;
      }
   }
   UNREACHABLE("invalid memory properties");
}
