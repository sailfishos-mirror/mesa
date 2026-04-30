/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _HALF_FLOAT_H_
#define _HALF_FLOAT_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "util/detect_arch.h"
#include "util/detect_cc.h"
#include "util/double.h"
#include "util/u_cpu_detect.h"

#if DETECT_ARCH_X86_64
#include <xmmintrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FP16_ONE     ((uint16_t) 0x3c00)
#define FP16_ZERO    ((uint16_t) 0)
#define FP16_MAX_F   65504.0

uint16_t _mesa_float_to_half_slow(float val);
float _mesa_half_to_float_slow(uint16_t val);
uint16_t _mesa_uint16_div_64k_to_half(uint16_t v);

/*
 * _mesa_float_to_float16_rtz_slow is no more than a wrapper to the counterpart
 * softfloat.h call. Still, softfloat.h conversion API is meant to be kept
 * private. In other words, only use the API published here, instead of
 * calling directly the softfloat.h one.
 */
uint16_t _mesa_float_to_float16_rtz_slow(float val);

static inline uint16_t
_mesa_float_to_half(float val)
{
#if DETECT_ARCH_X86_64 && DETECT_CC_GCC
   if (util_get_cpu_caps()->has_f16c) {
      __m128 in = {val};
      __m128i out;

      /* $0 = round to nearest */
      __asm volatile("vcvtps2ph $0, %1, %0" : "=v"(out) : "v"(in));
      return out[0];
   }
#endif
   return _mesa_float_to_half_slow(val);
}

static inline float
_mesa_half_to_float(uint16_t val)
{
#if DETECT_ARCH_X86_64 && DETECT_CC_GCC
   if (util_get_cpu_caps()->has_f16c) {
      __m128i in = {val};
      __m128 out;

      __asm volatile("vcvtph2ps %1, %0" : "=v"(out) : "v"(in));
      return out[0];
   }
#elif DETECT_ARCH_AARCH64 && DETECT_CC_GCC
   float result;
   uint16_t in = val;

   __asm volatile(
     "fcvt %s0, %h1\n"
     : "=w"(result)
     : "w"(in)
   );
   return result;
#endif
   return _mesa_half_to_float_slow(val);
}

static inline uint16_t
_mesa_float_to_float16_rtz(float val)
{
#if DETECT_ARCH_X86_64 && DETECT_CC_GCC
   if (util_get_cpu_caps()->has_f16c) {
      __m128 in = {val};
      __m128i out;

      /* $3 = round towards zero (truncate) */
      __asm volatile("vcvtps2ph $3, %1, %0" : "=v"(out) : "v"(in));
      return out[0];
   }
#endif
   return _mesa_float_to_float16_rtz_slow(val);
}

uint16_t _mesa_float_to_float16_ru(float val);
uint16_t _mesa_float_to_float16_rd(float val);

static inline uint16_t
_mesa_float_to_float16_rtne(float val)
{
   return _mesa_float_to_half(val);
}

static inline bool
_mesa_half_is_negative(uint16_t h)
{
   return !!(h & 0x8000);
}

static inline bool
_mesa_float_is_half(double val)
{
   /* val parameter is double to prevent implicit double->float cast.  We have
    * to cast to float because that's what _mesa_float_to_half expects and we
    * don't have any readily available _double_to_half function.  This may
    * introduce double-rounding errors, however this is ok because the final
    * check is done at double precision, any rounding will fail to produce the
    * original value.
    */
   uint16_t fp16_val = _mesa_float_to_half((float) val);
   bool is_denorm = (fp16_val & 0x7fff) != 0 && (fp16_val & 0x7fff) <= 0x3ff;
   return val == (double) _mesa_half_to_float(fp16_val) && !is_denorm;
}

/*
 * We round down from double to half float by going through float in between,
 * but this can give us inaccurate results in some cases.
 * One such case is 0x40ee6a0000000001, which should round to 0x7b9b, but
 * going through float first turns into 0x7b9a instead. This is because the
 * first non-fitting bit is set, so we get a tie, but with the least
 * significant bit of the original number set, the tie should break rounding
 * up.
 * The cast to float, however, turns into 0x47735000, which when going to half
 * still ties, but now we lost the tie-up bit, and instead we round to the
 * nearest even, which in this case is down.
 *
 * To fix this, we check if the original would have tied, and if the tie would
 * have rounded up, and if both are true, set the least significant bit of the
 * intermediate float to 1, so that a tie on the next cast rounds up as well.
 * If the rounding already got rid of the tie, that set bit will just be
 * truncated anyway and the end result doesn't change.
 *
 * Another failing case is 0x40effdffffffffff. This one doesn't have the tie
 * from double to half, so it just rounds down to 0x7bff (65504.0), but going
 * through float first, it turns into 0x477ff000, which does have the tie bit
 * for half set, and when that one gets rounded it turns into 0x7c00
 * (Infinity).
 * The fix for that one is to make sure the intermediate float does not have
 * the tie bit set if the original didn't have it.
 */
static inline uint16_t
_mesa_double_to_float16_rtne(double val)
{
   int significand_bits16 = 10;
   int significand_bits32 = 23;
   int significand_bits64 = 52;
   int f64_to_16_tie_bit = significand_bits64 - significand_bits16 - 1;
   int f32_to_16_tie_bit = significand_bits32 - significand_bits16 - 1;
   uint64_t f64_rounds_up_mask = ((1ULL << f64_to_16_tie_bit) - 1);

   union di src;
   union fi dst;

   src.d = val;
   dst.f = val;

   bool f64_has_tie = (src.ui & (1ULL << f64_to_16_tie_bit)) != 0;
   bool f64_rounds_up = (src.ui & f64_rounds_up_mask) != 0;

   dst.ui |= (f64_has_tie && f64_rounds_up);
   if (!f64_has_tie)
      dst.ui &= ~(1U << f32_to_16_tie_bit);

   return _mesa_float_to_float16_rtne(dst.f);
}

/*
 * double -> float -> half with RTZ doesn't have as many complications as
 * RTNE, but we do need to ensure that the double -> float cast also uses RTZ.
 */
static inline uint16_t
_mesa_double_to_float16_rtz(double val)
{
   return _mesa_float_to_float16_rtz(_mesa_double_to_float_rtz(val));
}

#ifdef __cplusplus

namespace mesa
{

/* Helper class for disambiguating fp16 from uint16_t in C++ overloads */

struct float16_t {
   uint16_t bits;
   float16_t(float f) : bits(_mesa_float_to_float16_rtne(f)) {}
   float16_t(double d) : bits(_mesa_double_to_float16_rtne(d)) {}
   float16_t(uint16_t raw_bits) : bits(raw_bits) {}
   static float16_t one() { return float16_t(FP16_ONE); }
   static float16_t zero() { return float16_t(FP16_ZERO); }
};

} /* namespace mesa */

#endif


#ifdef __cplusplus
} /* extern C */
#endif

#endif /* _HALF_FLOAT_H_ */
