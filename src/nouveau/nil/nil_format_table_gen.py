# Copyright 2024 Collabora Ltd.
# SPDX-License-Identifier: MIT

"""Generates nil_format_table.c"""

import argparse
import csv
import os

from mako import template

TEMPLATE_H = template.Template(text="""\
/* Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef ${guard}
#define ${guard}

#include "util/format/u_format.h"

enum nil_format_support_flags {
   NIL_FORMAT_SUPPORTS_TEXTURE_BIT        = BITFIELD_BIT(0),
   NIL_FORMAT_SUPPORTS_BUFFER_BIT         = BITFIELD_BIT(1),
   NIL_FORMAT_SUPPORTS_STORAGE_BIT        = BITFIELD_BIT(2),
   NIL_FORMAT_SUPPORTS_RENDER_BIT         = BITFIELD_BIT(3),
   NIL_FORMAT_SUPPORTS_ALPHA_BLEND_BIT    = BITFIELD_BIT(4),
   NIL_FORMAT_SUPPORTS_DEPTH_STENCIL_BIT  = BITFIELD_BIT(5),
   NIL_FORMAT_SUPPORTS_SCANOUT_BIT        = BITFIELD_BIT(6),
};

struct nil_tic_format {
   unsigned comp_sizes:8;
   unsigned type_r:3;
   unsigned type_g:3;
   unsigned type_b:3;
   unsigned type_a:3;
   unsigned src_x:3;
   unsigned src_y:3;
   unsigned src_z:3;
   unsigned src_w:3;
};

struct nil_format_info {
   unsigned czt:8;
   unsigned support:20;
   unsigned tic_v2_data_type:4;
   struct nil_tic_format tic;
};

extern const struct nil_format_info nil_format_table[PIPE_FORMAT_COUNT];

#endif /* ${guard} */
""");

TEMPLATE_C = template.Template(text="""\
/* Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

/* This file is autogenerated by nil_format_table_gen.py. DO NOT EDIT! */

#include "nil_format_table.h"

#include "cl9097.h"
#include "cl9097tex.h"
#include "clb097.h"
#include "clb097tex.h"
#include "clb197.h"
#include "clb197tex.h"
#include "clcb97.h"
#include "clcb97tex.h"

const struct nil_format_info nil_format_table[PIPE_FORMAT_COUNT] = {
% for f in formats:
    [PIPE_FORMAT_${f.pipe}] = {
        .czt = ${f.czt()},
        .support = ${f.support()},
        .tic_v2_data_type = ${f.v2_data_type()},
        .tic = {
            .comp_sizes = ${f.tcs()},
            .type_r = ${f.type(0)},
            .type_g = ${f.type(1)},
            .type_b = ${f.type(2)},
            .type_a = ${f.type(3)},
            .src_x = ${f.src(0)},
            .src_y = ${f.src(1)},
            .src_z = ${f.src(2)},
            .src_w = ${f.src(3)},
        },
    },
% endfor
};
""");

CT_FORMAT_PREFIX = {
    None        : 'NV9097_SET_COLOR_TARGET_FORMAT_V_',
    'maxwella'  : 'NVB097_SET_COLOR_TARGET_FORMAT_V_',
    'tk1'       : 'NVB097_SET_COLOR_TARGET_FORMAT_V_',
}

ZT_FORMAT_PREFIX = {
    None        : 'NV9097_SET_ZT_FORMAT_V_',
    'maxwella'  : 'NVB097_SET_ZT_FORMAT_V_',
    'maxwellb'  : 'NVB197_SET_ZT_FORMAT_V_',
    'tk1'       : 'NVB097_SET_ZT_FORMAT_V_',
}

TCS_PREFIX = {
    None        : 'NV9097_TEXHEADV2_0_COMPONENT_SIZES_',
    'maxwella'  : 'NVB097_TEXHEAD_BL_COMPONENTS_SIZES_',
    'maxwellb'  : 'NVB197_TEXHEAD_BL_COMPONENTS_SIZES_',
    'tk1'       : 'NVB097_TEXHEAD_BL_COMPONENTS_SIZES_',
    'hopper'    : 'NVCB97_TEXHEAD_V2_BL_COMPONENTS_SIZES_',
}

DATA_TYPES = {
    'S' : 'NV9097_TEXHEADV2_0_R_DATA_TYPE_NUM_SNORM',
    'N' : 'NV9097_TEXHEADV2_0_R_DATA_TYPE_NUM_UNORM',
    'I' : 'NV9097_TEXHEADV2_0_R_DATA_TYPE_NUM_SINT',
    'U' : 'NV9097_TEXHEADV2_0_R_DATA_TYPE_NUM_UINT',
    'F' : 'NV9097_TEXHEADV2_0_R_DATA_TYPE_NUM_FLOAT',
}

V2_DATA_TYPES = {
    'N' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_UNORM',
    'S' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_SNORM',
    'F' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_FLOAT',
    'SGNRGB' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_SGNRGB',
    'SGNA' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_SGNA',
    'U' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_UINT',
    'I' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_SINT',
    'ZS' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_ZS',
    'SZ' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_SZ',
    'ZFS' : 'NVCB97_TEXHEAD_V2_BL_DATA_TYPE_TEX_DATA_TYPE_ZFS',
}

SOURCES = {
    '0' : 'NV9097_TEXHEADV2_0_Z_SOURCE_IN_ZERO',
    'R' : 'NV9097_TEXHEADV2_0_Z_SOURCE_IN_R',
    'G' : 'NV9097_TEXHEADV2_0_Z_SOURCE_IN_G',
    'B' : 'NV9097_TEXHEADV2_0_Z_SOURCE_IN_B',
    'A' : 'NV9097_TEXHEADV2_0_Z_SOURCE_IN_A',
    '1' : 'NV9097_TEXHEADV2_0_Z_SOURCE_IN_ONE',
}

SUPPORT_FLAGS = {
    'T' : 'NIL_FORMAT_SUPPORTS_TEXTURE_BIT',
    'B' : 'NIL_FORMAT_SUPPORTS_BUFFER_BIT',
    'S' : 'NIL_FORMAT_SUPPORTS_STORAGE_BIT',
    'C' : 'NIL_FORMAT_SUPPORTS_RENDER_BIT',
    'A' : 'NIL_FORMAT_SUPPORTS_ALPHA_BLEND_BIT',
    'Z' : 'NIL_FORMAT_SUPPORTS_DEPTH_STENCIL_BIT',
    'D' : 'NIL_FORMAT_SUPPORTS_SCANOUT_BIT',
}

class Format(object):
    def __init__(self, line):
        self.pipe = line[0].strip()
        self._czt = line[1].strip()
        self._tcs = line[2].strip()
        self._types = list(line[3].strip())
        self._data_type = line[4].strip()
        self._srcs = list(line[5].strip())
        self._support = list(line[6].strip())
        if len(line) > 7:
            self.hw = line[7].strip().lower()
        else:
            self.hw = None

    def czt(self):
        if self._czt == 'NONE':
            return CT_FORMAT_PREFIX[self.hw] + 'DISABLED'
        elif 'Z' in self._support:
            return ZT_FORMAT_PREFIX[self.hw] + self._czt
        else:
            return CT_FORMAT_PREFIX[self.hw] + self._czt

    def support(self):
        return ' | '.join(SUPPORT_FLAGS[f] for f in self._support)

    def tcs(self):
        return TCS_PREFIX[self.hw] + self._tcs

    def type(self, comp):
        if comp < len(self._types):
            return DATA_TYPES[self._types[comp]]
        else:
            return DATA_TYPES[self._types[0]]

    def v2_data_type(self):
        return V2_DATA_TYPES[self._data_type]

    def src(self, comp):
        if self._srcs[comp] == '1':
            # Find any component which isn't a 0/1 and look at that
            # component's data type to determine whether or not 1 is an
            # integer.
            for s in self._srcs:
                c = 'RGBA'.find(s)
                if c >= 0:
                    if self._types[c] in 'SNF':
                        return SOURCES['1'] + '_FLOAT'
                    else:
                        return SOURCES['1'] + '_INT'
            assert False, self.pipe + ': Failed to find non-constant component'
        else:
            c = 'RGBA'.find(self._srcs[comp])
            assert c < len(self._types), self.pipe + ': Swizzle is out of bounds'
            return SOURCES[self._srcs[comp]]

def reader(csvfile):
    """Wrapper around csv.reader that skips comments and blanks."""
    # csv.reader actually reads the file one line at a time (it was designed to
    # open excel generated sheets), so hold the file until all of the lines are
    # read.
    with open(csvfile, 'r') as f:
        for line in csv.reader(f):
            if line and not line[0].startswith('#'):
                yield line

def main():
    """Main function."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv', action='store', help='The CSV file to parse.')
    parser.add_argument('--out-c', required=True, help='Output C file.')
    parser.add_argument('--out-h', required=True, help='Output H file.')
    args = parser.parse_args()

    formats = [Format(l) for l in reader(args.csv)]

    try:
        with open(args.out_h, 'w', encoding='utf-8') as f:
            guard = os.path.basename(args.out_h).replace('.', '_').upper()
            f.write(TEMPLATE_H.render(guard=guard))
        with open(args.out_c, 'w', encoding='utf-8') as f:
            f.write(TEMPLATE_C.render(formats=formats))

    except Exception:
        # In the event there's an error, this imports some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        import sys
        from mako import exceptions
        print(exceptions.text_error_template().render(), file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
