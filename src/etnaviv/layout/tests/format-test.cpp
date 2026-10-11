/*
 * Copyright © 2026 Daniel Lang
 * SPDX-License-Identifier: MIT
 */

#include "eil_format.h"

#include "hw/common_3d.xml.h"
#include "hw/state.xml.h"
#include "hw/state_3d.xml.h"

#include <gtest/gtest.h>

TEST(FormatTable, get_entry_undefined)
{
   auto entry = eil_get_format_entry(PIPE_FORMAT_NV20);
   EXPECT_NE(nullptr, entry);
   EXPECT_FALSE(entry->has_vtx);
   EXPECT_FALSE(entry->has_tex);
   EXPECT_FALSE(entry->has_pe);
}

#ifndef NDEBUG
TEST(FormatTable, get_entry_invalid)
{
   EXPECT_DEATH(eil_get_format_entry(static_cast<pipe_format>(PIPE_FORMAT_COUNT + 1U)), "");
   EXPECT_DEATH(eil_get_format_entry(static_cast<pipe_format>(-1)), "");
}
#endif

TEST(FormatTable, get_entry)
{
   auto entry = eil_get_format_entry(PIPE_FORMAT_R8G8B8A8_UNORM);
   EXPECT_NE(nullptr, entry);
   EXPECT_TRUE(entry->has_vtx);
   EXPECT_TRUE(entry->has_tex);
   EXPECT_TRUE(entry->has_pe);
   EXPECT_FALSE(entry->tex_ext);
   EXPECT_FALSE(entry->tex_astc);
   EXPECT_EQ(FE_DATA_TYPE_UNSIGNED_BYTE, entry->vtx);
   EXPECT_EQ(TEXTURE_FORMAT_A8R8G8B8, entry->tex);
   EXPECT_EQ(PE_FORMAT_A8R8G8B8, entry->pe);
}

TEST(FormatTable, get_entry_ext)
{
   auto entry = eil_get_format_entry(PIPE_FORMAT_R8G8B8X8_SNORM);
   EXPECT_NE(nullptr, entry);
   EXPECT_TRUE(entry->has_tex);
   EXPECT_FALSE(entry->has_vtx);
   EXPECT_EQ(0, entry->vtx);
   EXPECT_TRUE(entry->tex_ext);
   EXPECT_FALSE(entry->tex_astc);
}

TEST(FormatTable, get_entry_astc)
{
   auto entry = eil_get_format_entry(PIPE_FORMAT_ASTC_4x4);
   EXPECT_NE(nullptr, entry);
   EXPECT_TRUE(entry->has_tex);
   EXPECT_FALSE(entry->has_vtx);
   EXPECT_EQ(0, entry->vtx);
   EXPECT_FALSE(entry->tex_ext);
   EXPECT_TRUE(entry->tex_astc);
}
