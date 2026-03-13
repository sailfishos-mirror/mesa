/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef _SHA1_H
#define _SHA1_H

/* This is not SHA1. This is BLAKE3 exposed as SHA1 functions due to
 * transitional and historic reasons.
 *
 * TODO: Remove this and use _mesa_blake3_* functions everywhere.
 * All remnants of SHA1 should be removed from Mesa except build_id.
 */

#include "util/mesa-blake3.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void
SHA1Init(blake3_hasher *context)
{
    _mesa_blake3_init(context);
}

static inline void
SHA1Update(blake3_hasher *context, const uint8_t *data, size_t len)
{
    _mesa_blake3_update(context, data, len);
}

static inline void
SHA1Final(uint8_t digest[BLAKE3_KEY_LEN], blake3_hasher *context)
{
    _mesa_blake3_final(context, digest);
}

#ifdef __cplusplus
}
#endif

#endif /* _SHA1_H */
