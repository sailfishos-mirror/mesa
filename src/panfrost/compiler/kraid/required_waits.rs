// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::flow::FlowWaitBit;
use crate::ir::{Instr, Op, Shader};
use crate::ops::OpNop;

fn op_requires_preceding_wait(op: &Op) -> &'static [FlowWaitBit] {
    match op {
        Op::ATest(_) => &[FlowWaitBit::ZS],
        Op::ZSEmit(_) => &[FlowWaitBit::ZS],
        Op::Blend(_) | Op::BlendCall(_) => {
            &[FlowWaitBit::ZS, FlowWaitBit::Barrier]
        }
        Op::LdTile(x) => {
            if x.is_resource {
                &[FlowWaitBit::Resource]
            } else if x.z_stencil {
                &[FlowWaitBit::ZS]
            } else {
                &[FlowWaitBit::Barrier]
            }
        }
        Op::StTile(_) => &[FlowWaitBit::Barrier],
        _ => &[],
    }
}

impl Shader<'_> {
    pub fn insert_required_waits(&mut self) {
        // TODO: Something similar to va_merge_flow
        // TODO: slot 6 / 7 / barrier are read-only*, they can be DCEd by a
        //       data-flow pass, we should also remove them completely in blend
        //       shaders.
        //       * = except for compute shaders that can signal it with BARRIER
        self.map_instrs(|i, _b| {
            let wait = op_requires_preceding_wait(&i.op);

            if wait.is_empty() {
                return [i].into();
            }

            let mut nop = Instr::from(OpNop {});
            for bit in wait {
                nop.flow.set_wait_bit(*bit);
            }
            [nop, i].into()
        });
    }
}
