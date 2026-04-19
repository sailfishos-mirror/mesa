/*
 * Copyright © 2017 Google.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_DEBUG_H
#define RADV_DEBUG_H

#include "radv_device.h"
#include "radv_instance.h"
#include "radv_physical_device.h"

uint32_t radv_find_memory_index(const struct radv_physical_device *pdev, VkMemoryPropertyFlags flags);

#endif /* RADV_DEBUG_H */
