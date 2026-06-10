// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::ops::*;

fn type_is_16bit(data_type: DataType) -> bool {
    data_type.total_bits() == 16
}

fn replicate_type(data_type: DataType, n: u8) -> DataType {
    DataType::v(data_type.comps() * n, data_type.scalar_type())
}

macro_rules! lower_op {
    ($op: expr, $variant: ident) => {{
        match $op.$variant.total_bits() {
            8 => {
                for src in $op.srcs() {
                    debug_assert!(src.replicates_byte());
                }
                $op.$variant = replicate_type($op.$variant, 4);
            }
            16 => {
                for src in $op.srcs() {
                    debug_assert!(src.replicates_half());
                }
                $op.$variant = replicate_type($op.$variant, 2);
            }
            bits => assert!(bits >= 32),
        }
    }};
}

fn lower_instr(instr: &mut Instr) {
    match &mut instr.op {
        Op::FAdd(op) => lower_op!(op, dst_type),
        Op::FCmp(op) => lower_op!(op, src_type),
        Op::IAdd(op) => lower_op!(op, dst_type),
        Op::ICmp(op) => lower_op!(op, src_type),
        Op::LdPka(_) | Op::Load(_) => (), // These handle 16-bit natively
        Op::Mov(op) => {
            if op.dst_type.total_bits() == 16 {
                instr.op = Op::from(OpIAdd {
                    dst: op.dst.clone(),
                    dst_type: DataType::V2I16,
                    saturate: false,
                    srcs: [0.into(), op.src.clone().half(0)],
                });
            }
        }
        Op::ShiftLop(op) => lower_op!(op, dst_type),
        op => {
            for dst in op.dsts() {
                assert!(dst.bytes_written() >= 4);
            }
        }
    }
}

impl Shader<'_> {
    pub fn lower_16bit_alu(&mut self) {
        for b in self.blocks.iter_mut() {
            for i in b.instrs.iter_mut() {
                lower_instr(i);
            }
        }
    }
}
