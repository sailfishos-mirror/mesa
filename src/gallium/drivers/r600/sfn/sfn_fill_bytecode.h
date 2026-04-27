/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef SFN_FILL_BYTECODE_H
#define SFN_FILL_BYTECODE_H

#include "../r600_asm.h"

namespace r600 {

class ExportInstr;
class FetchInstr;
class GDSInstr;
class MemRingOutInstr;
class ScratchIOInstr;
class StreamOutInstr;
class TexInstr;
class WriteTFInstr;

bool emit_bytecode_tex(r600_bytecode& bc, const TexInstr& tex_instr);

bool emit_bytecode_export_alpha_to_coverage(r600_bytecode& bc,
                                            const ExportInstr& export_instr);

bool emit_bytecode_export(r600_bytecode& bc,
                          const ExportInstr& export_instr,
                          bool ps_alpha_to_one);

bool emit_bytecode_scratch(r600_bytecode& bc, const ScratchIOInstr& instr);

bool emit_bytecode_stream_out(r600_bytecode& bc, const StreamOutInstr& instr);

bool emit_bytecode_mem_ring(r600_bytecode& bc, const MemRingOutInstr& instr);

bool emit_bytecode_fetch(r600_bytecode& bc, const FetchInstr& fetch_instr, bool use_tc);

bool emit_bytecode_gds(r600_bytecode& bc, const GDSInstr& instr);

bool emit_bytecode_tf_write(r600_bytecode& bc, const WriteTFInstr& instr);

} // namespace r600

#endif