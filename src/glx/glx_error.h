/*
 Copyright (c) 2009 Apple Inc.
 
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

#ifndef GLX_ERROR_H
#define GLX_ERROR_H

#include <stdbool.h>
#include <stdint.h>
#include <X11/Xlib.h>
#include <xcb/xcb.h>

#ifdef __cplusplus
extern "C" {
#endif

void __glXSendError(Display * dpy, int_fast8_t errorCode,
          uint_fast32_t resourceID, uint_fast16_t minorCode,
          bool coreX11error);

void __glXSendErrorForXcb(Display * dpy,
                const xcb_generic_error_t *err);

#ifdef __cplusplus
}
#endif

#endif
