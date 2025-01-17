/*
 * Copyright © 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#ifndef AC_VCN_H
#define AC_VCN_H

#define RADEON_VCN_ENGINE_INFO                                        (0x30000001)
#define RADEON_VCN_SIGNATURE                                          (0x30000002)
#define RADEON_VCN_ENGINE_TYPE_COMMON                                 (0x00000001)
#define RADEON_VCN_ENGINE_TYPE_ENCODE                                 (0x00000002)
#define RADEON_VCN_ENGINE_TYPE_DECODE                                 (0x00000003)

#define RADEON_VCN_ENGINE_INFO_SIZE                                   (0x00000010)
#define RADEON_VCN_SIGNATURE_SIZE                                     (0x00000010)

#define RADEON_VCN_IB_COMMON_OP_WRITEMEMORY                           (0x33000001)

struct rvcn_sq_var {
   unsigned int *signature_ib_checksum;
   unsigned int *signature_ib_total_size_in_dw;
   unsigned int *engine_ib_size_of_packages;
};

struct rvcn_cmn_engine_ib_package {
   unsigned int package_size;
   unsigned int package_type;
};

struct rvcn_cmn_engine_op_writememory {
    unsigned int dest_addr_lo;           // Low address of memory
    unsigned int dest_addr_hi;           // High address of memory
    unsigned int data;                   // data to be written
};

#endif
