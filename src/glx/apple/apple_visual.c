/*
 Copyright (c) 2008, 2009 Apple Inc.
 
 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation files
 (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge,
 publish, distribute, sublicense, and/or sell copies of the Software,
 and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:
 
 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT.  IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT
 HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 DEALINGS IN THE SOFTWARE.
 
 Except as contained in this notice, the name(s) of the above
 copyright holders shall not be used in advertising or otherwise to
 promote the sale, use or other dealings in this Software without
 prior written authorization.
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <GL/gl.h>
#include <util/u_debug.h>

/* <rdar://problem/6953344> */
#define glTexImage1D glTexImage1D_OSX
#define glTexImage2D glTexImage2D_OSX
#define glTexImage3D glTexImage3D_OSX
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/CGLRenderers.h>
#include <OpenGL/CGLTypes.h>
#undef glTexImage1D
#undef glTexImage2D
#undef glTexImage3D

#ifndef kCGLPFAOpenGLProfile
#define kCGLPFAOpenGLProfile 99
#endif

#ifndef kCGLOGLPVersion_3_2_Core
#define kCGLOGLPVersion_3_2_Core 0x3200
#endif

#include "apple_cgl.h"
#include "apple_visual.h"
#include "apple_glx.h"
#include "glxconfig.h"

enum
{
   MAX_ATTR = 60
};

CGLError
apple_visual_create_pfobj(CGLPixelFormatObj * pfobj, const struct glx_config * mode,
                          bool * double_buffered, bool * uses_stereo,
                          bool offscreen)
{
   CGLPixelFormatAttribute attr[MAX_ATTR];
   int numattr = 0;
   GLint vsref = 0;
   CGLError error = 0;
   bool use_core_profile = debug_get_bool_option("LIBGL_PROFILE_CORE", false);

   *pfobj = NULL;

   if (offscreen) {
      apple_glx_log_debug("offscreen rendering enabled.  Using kCGLPFAOffScreen");

      attr[numattr++] = kCGLPFAOffScreen;
   }
   else if (debug_get_bool_option("LIBGL_ALWAYS_SOFTWARE", false)) {
      apple_glx_log_debug("Software rendering requested.  Using kCGLRendererGenericFloatID.");
      attr[numattr++] = kCGLPFARendererID;
      attr[numattr++] = kCGLRendererGenericFloatID;
   }
   else if (debug_get_bool_option("LIBGL_ALLOW_SOFTWARE", false)) {
      apple_glx_log_debug("Software rendering is not being excluded.  Not using kCGLPFAAccelerated.");
   }
   else {
      attr[numattr++] = kCGLPFAAccelerated;
   }

   /*
    * The program chose a config based on the fbconfigs or visuals.
    * Those are based on the attributes from CGL, so we probably
    * do want the closest match for the color, depth, and accum.
    */
   attr[numattr++] = kCGLPFAClosestPolicy;

   if (mode->stereoMode) {
      attr[numattr++] = kCGLPFAStereo;
      *uses_stereo = true;
   }
   else {
      *uses_stereo = false;
   }

   if (!offscreen && mode->doubleBufferMode) {
      attr[numattr++] = kCGLPFADoubleBuffer;
      *double_buffered = true;
   }
   else {
      *double_buffered = false;
   }

   attr[numattr++] = kCGLPFAColorSize;
   attr[numattr++] = mode->redBits + mode->greenBits + mode->blueBits;
   attr[numattr++] = kCGLPFAAlphaSize;
   attr[numattr++] = mode->alphaBits;

   if ((mode->accumRedBits + mode->accumGreenBits + mode->accumBlueBits) > 0) {
      attr[numattr++] = kCGLPFAAccumSize;
      attr[numattr++] = mode->accumRedBits + mode->accumGreenBits +
         mode->accumBlueBits + mode->accumAlphaBits;
   }

   if (mode->depthBits > 0) {
      attr[numattr++] = kCGLPFADepthSize;
      attr[numattr++] = mode->depthBits;
   }

   if (mode->stencilBits > 0) {
      attr[numattr++] = kCGLPFAStencilSize;
      attr[numattr++] = mode->stencilBits;
   }

   if (mode->sampleBuffers > 0) {
      attr[numattr++] = kCGLPFAMultisample;
      attr[numattr++] = kCGLPFASampleBuffers;
      attr[numattr++] = mode->sampleBuffers;
      attr[numattr++] = kCGLPFASamples;
      attr[numattr++] = mode->samples;
   }

   /* Debugging support for Core profiles to support newer versions of OpenGL */
   if (use_core_profile) {
      attr[numattr++] = kCGLPFAOpenGLProfile;
      attr[numattr++] = kCGLOGLPVersion_3_2_Core;
   }

   attr[numattr++] = 0;

   assert(numattr < MAX_ATTR);

   error = apple_cgl.choose_pixel_format(attr, pfobj, &vsref);

   if (error != kCGLNoError) {
      apple_glx_log_error("CGLChoosePixelFormat error: %s",
                          apple_cgl.error_string(error));
      *pfobj = NULL;
      return error;
   }

   if (!*pfobj || vsref == 0) {
      apple_glx_log_error("No matching pixelformats found, "
                          "perhaps try setting LIBGL_ALLOW_SOFTWARE=true");
      if (*pfobj) {
         apple_cgl.destroy_pixel_format(*pfobj);
         *pfobj = NULL;
      }
      return kCGLBadPixelFormat;
   }

   return kCGLNoError;
}
