// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::flow::FlowWaitBit;
use crate::ir::*;

fn op_is_message(op: &Op) -> bool {
    // TODO: Make this real
    matches!(
        op,
        Op::LdPka(_) | Op::LeaPka(_) | Op::Load(_) | Op::Store(_)
    )
}

impl Shader<'_> {
    pub fn assign_message_slots(&mut self) {
        for b in self.blocks.iter_mut() {
            for i in b.instrs.iter_mut() {
                if op_is_message(&i.op) {
                    i.flow.set_msg_slot_idx(0);
                    i.flow.set_wait_bit(FlowWaitBit::Slot0);
                }
            }
        }
    }
}
