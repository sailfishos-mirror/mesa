/* Copyright © 2007 Carl Worth
 * Copyright © 2009 Jeremy Huddleston, Julien Cristau, and Matthieu Herrb
 * Copyright © 2009-2010 Mikhail Gusarov
 * Copyright © 2012 Yaakov Selkowitz and Keith Packard
 * Copyright © 2014 Intel Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "mesa-blake3.h"
#include "mesa-sha1.h"
#include "hex.h"
#include <string.h>
#include <inttypes.h>

void
_mesa_sha1_compute(const void *data, size_t size, unsigned char result[BLAKE3_KEY_LEN])
{
   blake3_hasher ctx;

   _mesa_blake3_init(&ctx);
   _mesa_blake3_update(&ctx, data, size);
   _mesa_blake3_final(&ctx, result);
}

void
_mesa_sha1_format(char *buf, const unsigned char *sha1)
{
   mesa_bytes_to_hex(buf, sha1, BLAKE3_KEY_LEN);
}

/* Convert a hashs string hexidecimal representation into its more compact
 * form.
 */
void
_mesa_sha1_hex_to_sha1(unsigned char *buf, const char *hex)
{
   mesa_hex_to_bytes(buf, hex, BLAKE3_KEY_LEN);
}

static void
sha1_to_uint32(const uint8_t sha1[BLAKE3_KEY_LEN],
               uint32_t out[BLAKE3_OUT_LEN32])
{
   memset(out, 0, BLAKE3_KEY_LEN);

   for (unsigned i = 0; i < BLAKE3_KEY_LEN; i++)
      out[i / 4] |= (uint32_t)sha1[i] << ((i % 4) * 8);
}

void
_mesa_sha1_print(FILE *f, const uint8_t sha1[BLAKE3_KEY_LEN])
{
   uint32_t u32[BLAKE3_KEY_LEN];
   sha1_to_uint32(sha1, u32);

   for (unsigned i = 0; i < BLAKE3_OUT_LEN32; i++) {
      fprintf(f, i ? ", 0x%08" PRIx32 : "0x%08" PRIx32, u32[i]);
   }
}

bool
_mesa_printed_sha1_equal(const uint8_t sha1[BLAKE3_KEY_LEN],
                         const uint32_t printed_sha1[BLAKE3_OUT_LEN32])
{
   uint32_t u32[BLAKE3_OUT_LEN32];
   sha1_to_uint32(sha1, u32);

   return memcmp(u32, printed_sha1, sizeof(u32)) == 0;
}
