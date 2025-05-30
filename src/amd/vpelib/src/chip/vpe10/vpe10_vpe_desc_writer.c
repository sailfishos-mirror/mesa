/* Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "vpe_assert.h"
#include "common.h"
#include "reg_helper.h"
#include "vpe10_vpe_desc_writer.h"
#include "vpe10_command.h"

void vpe10_construct_vpe_desc_writer(struct vpe_desc_writer *writer)
{
    writer->init            = vpe10_vpe_desc_writer_init;
    writer->add_plane_desc  = vpe10_vpe_desc_writer_add_plane_desc;
    writer->add_config_desc = vpe10_vpe_desc_writer_add_config_desc;
    writer->complete        = vpe10_vpe_desc_writer_complete;
}

enum vpe_status vpe10_vpe_desc_writer_init(
    struct vpe_desc_writer *writer, struct vpe_buf *buf, int cd)
{
    uint32_t *cmd_space;
    uint64_t  size = sizeof(uint32_t);

    writer->base_cpu_va      = buf->cpu_va;
    writer->base_gpu_va      = buf->gpu_va;
    writer->buf              = buf;
    writer->num_config_desc  = 0;
#ifdef VPE_REGISTER_PROFILE
    writer->reuse_num_config_dec = 0;
#endif
    writer->plane_desc_added = false;
    writer->status           = VPE_STATUS_OK;

    if (buf->size < size) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return writer->status;
    }

    if (writer->status == VPE_STATUS_OK) {
        cmd_space    = (uint32_t *)(uintptr_t)writer->buf->cpu_va;
        *cmd_space++ = VPE_DESC_CMD_HEADER(cd);

        writer->buf->cpu_va += size;
        writer->buf->gpu_va += size;
        writer->buf->size -= size;
    }

    return writer->status;
}

void vpe10_vpe_desc_writer_add_plane_desc(
    struct vpe_desc_writer *writer, uint64_t plane_desc_addr, uint8_t tmz)
{
    uint32_t *cmd_space;
    uint64_t  size = 3 * sizeof(uint32_t);

    if (writer->status != VPE_STATUS_OK)
        return;

    /* Buffer does not have enough space to write */
    if (writer->buf->size < size) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return;
    }

    cmd_space = (uint32_t *)(uintptr_t)writer->buf->cpu_va;

    VPE_ASSERT(!(plane_desc_addr & 0x3));
    VPE_ASSERT(!writer->plane_desc_added);

    *cmd_space++ = (ADDR_LO(plane_desc_addr) | (unsigned)(tmz & 1));
    *cmd_space++ = ADDR_HI(plane_desc_addr);

    // skip the DW3 as well, which is finalized during complete

    writer->buf->cpu_va += size;
    writer->buf->gpu_va += size;
    writer->buf->size -= size;
    writer->plane_desc_added = true;
}

void vpe10_vpe_desc_writer_add_config_desc(
    struct vpe_desc_writer *writer, uint64_t config_desc_addr, bool reuse, uint8_t tmz)
{
    uint32_t *cmd_space;
    uint64_t  size = 2 * sizeof(uint32_t);

    if (writer->status != VPE_STATUS_OK)
        return;

    /* Buffer does not have enough space to write */
    if (writer->buf->size < size) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return;
    }

    cmd_space = (uint32_t *)(uintptr_t)writer->buf->cpu_va;

    VPE_ASSERT(!(config_desc_addr & 0x3));

    *cmd_space++ = (ADDR_LO(config_desc_addr) | ((unsigned)reuse << 1) | (unsigned)(tmz & 1));
    *cmd_space++ = ADDR_HI(config_desc_addr);

    writer->buf->cpu_va += size;
    writer->buf->gpu_va += size;
    writer->buf->size -= size;
    writer->num_config_desc++;
#ifdef VPE_REGISTER_PROFILE
    if (reuse)
        writer->reuse_num_config_dec++;
#endif
}

void vpe10_vpe_desc_writer_complete(struct vpe_desc_writer *writer)
{
    uint32_t *cmd_space;

    if (writer->status != VPE_STATUS_OK)
        return;

    // NUM_CONFIG_DESCRIPTOR is at DW3
    cmd_space = (uint32_t *)(uintptr_t)(writer->base_cpu_va + 3 * sizeof(uint32_t));

    VPE_ASSERT(!(writer->num_config_desc & 0xFFFFFF00));
    VPE_ASSERT(writer->num_config_desc > 0);
    // NUM_CONFIG_DESCRIPTOR is 1-based
    *cmd_space = (writer->num_config_desc - 1) & 0xFF;
}
