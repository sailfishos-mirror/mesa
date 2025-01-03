/*
 * Copyright 2008 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright 2011 Marek Olšák <maraeo@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "r300_chipset.h"
#include "winsys/radeon_winsys.h"

#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_process.h"

#include <stdio.h>
#include <errno.h>

/* r300_chipset: A file all to itself for deducing the various properties of
 * Radeons. */

/* Parse a PCI ID and fill an r300_capabilities struct with information. */
void r300_parse_chipset(uint32_t pci_id, struct r300_capabilities* caps)
{
    switch (pci_id) {
#define CHIPSET(pci_id, name, chipfamily) \
        case pci_id: \
            caps->family = CHIP_##chipfamily; \
            break;
#include "pci_ids/r300_pci_ids.h"
#undef CHIPSET

    default:
        fprintf(stderr, "r300: Warning: Unknown chipset 0x%x\nAborting...",
                pci_id);
        abort();
    }

    /* Defaults. */
    caps->high_second_pipe = false;
    caps->num_vert_fpus = 0;
    caps->hiz_ram = 0;
    caps->zmask_ram = 0;
    caps->has_cmask = false;


    switch (caps->family) {
    case CHIP_R300:
    case CHIP_R350:
        caps->high_second_pipe = true;
        caps->num_vert_fpus = 4;
        caps->has_cmask = true; /* guessed because there is also HiZ */
        caps->hiz_ram = R300_HIZ_LIMIT;
        caps->zmask_ram = PIPE_ZMASK_SIZE;
        break;

    case CHIP_RV350:
    case CHIP_RV370:
        caps->high_second_pipe = true;
        caps->num_vert_fpus = 2;
        caps->zmask_ram = RV3xx_ZMASK_SIZE;
        break;

    case CHIP_RV380:
        caps->high_second_pipe = true;
        caps->num_vert_fpus = 2;
        caps->has_cmask = true; /* guessed because there is also HiZ */
        caps->hiz_ram = R300_HIZ_LIMIT;
        caps->zmask_ram = RV3xx_ZMASK_SIZE;
        break;

    case CHIP_RS400:
    case CHIP_RS600:
    case CHIP_RS690:
    case CHIP_RS740:
        break;

    case CHIP_RC410:
    case CHIP_RS480:
        caps->zmask_ram = RV3xx_ZMASK_SIZE;
        break;

    case CHIP_R420:
    case CHIP_R423:
    case CHIP_R430:
    case CHIP_R480:
    case CHIP_R481:
    case CHIP_RV410:
        caps->num_vert_fpus = 6;
        caps->has_cmask = true; /* guessed because there is also HiZ */
        caps->hiz_ram = R300_HIZ_LIMIT;
        caps->zmask_ram = PIPE_ZMASK_SIZE;
        break;

    case CHIP_R520:
        caps->num_vert_fpus = 8;
        caps->has_cmask = true;
        caps->hiz_ram = R300_HIZ_LIMIT;
        caps->zmask_ram = PIPE_ZMASK_SIZE;
        break;

    case CHIP_RV515:
        caps->num_vert_fpus = 2;
        caps->has_cmask = true;
        caps->hiz_ram = R300_HIZ_LIMIT;
        caps->zmask_ram = PIPE_ZMASK_SIZE;
        break;

    case CHIP_RV530:
        caps->num_vert_fpus = 5;
        caps->has_cmask = true;
        caps->hiz_ram = RV530_HIZ_LIMIT;
        caps->zmask_ram = PIPE_ZMASK_SIZE;
        break;

    case CHIP_R580:
    case CHIP_RV560:
    case CHIP_RV570:
        caps->num_vert_fpus = 8;
        caps->has_cmask = true;
        caps->hiz_ram = RV530_HIZ_LIMIT;
        caps->zmask_ram = PIPE_ZMASK_SIZE;
        break;
    }

    caps->num_tex_units = 16;
    caps->is_r400 = caps->family >= CHIP_R420 && caps->family < CHIP_RV515;
    caps->is_r500 = caps->family >= CHIP_RV515;
    caps->is_rv350 = caps->family >= CHIP_RV350;
    caps->z_compress = caps->is_rv350 ? R300_ZCOMP_8X8 : R300_ZCOMP_4X4;
    caps->dxtc_swizzle = caps->is_r400 || caps->is_r500;
    caps->has_us_format = caps->family == CHIP_R520;
    caps->has_tcl = caps->num_vert_fpus > 0;
}
