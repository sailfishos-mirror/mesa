
template = """\
/*
 * Copyright (c) 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 * This file was generated by aco_opcodes_h.py
 */

#ifndef _ACO_OPCODES_
#define _ACO_OPCODES_

#include <stdint.h>

namespace aco {

enum aco_base_type : uint8_t {
% for e in AcoBaseType:
   ${e.name} = ${hex(e.value)},
% endfor
};

enum fixed_reg : uint8_t {
% for e in FixedReg:
   ${e.name} = ${hex(e.value)},
% endfor
};

enum class Format : uint16_t {
% for e in Format:
   ${e.name} = ${hex(e.value)},
% endfor
};

enum class instr_class : uint8_t {
% for name in InstrClass:
   ${name.value},
% endfor
   count,
};

<% opcode_names = sorted(instructions.keys()) %>

enum class aco_opcode : uint16_t {
% for name in opcode_names:
   ${name},
% endfor
   last_opcode = ${opcode_names[-1]},
   num_opcodes = last_opcode + 1
};

}
#endif /* _ACO_OPCODES_ */"""

from aco_opcodes import instructions, InstrClass, Format, AcoBaseType, FixedReg
from mako.template import Template

print(Template(template).render(instructions=instructions, InstrClass=InstrClass, Format=Format, AcoBaseType=AcoBaseType, FixedReg=FixedReg))
