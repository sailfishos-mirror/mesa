/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GfxStreamVulkanConnection.h"

GfxStreamVulkanConnection::GfxStreamVulkanConnection(gfxstream::guest::IOStream* stream) {
    mVkEnc = std::make_unique<gfxstream::vk::VkEncoder>(stream);
}

GfxStreamVulkanConnection::~GfxStreamVulkanConnection() {}

void* GfxStreamVulkanConnection::getEncoder() { return mVkEnc.get(); }