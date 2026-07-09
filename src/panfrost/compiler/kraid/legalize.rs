// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::mem;

use crate::builder::*;
use crate::ir::*;
use crate::ops::OpMov;
use compiler::bitset::BitSet;

fn legalize_imm_src(b: &mut impl SSABuilder, op: &mut Op, src_idx: usize) {
    let src = &op.srcs()[src_idx];
    let SrcRef::Imm32(imm32) = &src.src_ref else {
        return;
    };
    if b.model().op_src_supports_imm32(op, src, (*imm32).into()) {
        return;
    }

    let ssa = b.alloc_ssa(32);
    let src = &mut op.srcs_mut()[src_idx];
    let src_ref = mem::replace(&mut src.src_ref, ssa.into());

    b.push_op(OpMov {
        dst: ssa.into(),
        dst_type: DataType::I32,
        src: src_ref.into(),
    });
}

#[derive(Default)]
struct SSAValueSet {
    set: BitSet<SSAValue>,
    vec: Vec<SSAValue>,
}

impl SSAValueSet {
    fn new() -> SSAValueSet {
        Default::default()
    }

    fn clear(&mut self) {
        for ssa in self.vec.drain(..) {
            self.set.remove(ssa);
        }
    }

    fn is_empty(&self) -> bool {
        self.vec.is_empty()
    }

    fn insert(&mut self, ssa: SSAValue) -> bool {
        if self.set.insert(ssa) {
            self.vec.push(ssa);
            true
        } else {
            false
        }
    }
}

/// Legalizes vector sources by ensuring the following
///
///  1. For any vector source (SSARef::comps() > 1), all the SSAValues
///     referenced by the SSARef are unique.
///
///  2. For any two vector sources, either they are identical or they have
///     no SSAValues in common.
fn legalize_vec_srcs(
    b: &mut impl SSABuilder,
    instr: &mut Instr,
    ssa_used: &mut SSAValueSet,
) {
    debug_assert!(ssa_used.is_empty());
    let srcs = instr.srcs_mut();

    let mut duplicates = [!0_usize; 4];
    debug_assert!(srcs.len() <= duplicates.len());
    for i in 0..srcs.len() {
        if !srcs[i].src_ref.as_ssa().is_some_and(|ssa| ssa.comps() > 1) {
            continue;
        }

        for j in 0..i {
            if srcs[i].src_ref == srcs[j].src_ref {
                duplicates[i] = j;
                break;
            }
        }
        if duplicates[i] != !0_usize {
            continue;
        }

        let vec = srcs[i].src_ref.as_mut_ssa().unwrap();
        for ssa in vec {
            if !ssa_used.insert(*ssa) {
                *ssa = b.copy_ssa(*ssa);
            }
        }
    }

    for i in 0..srcs.len() {
        if duplicates[i] != !0_usize {
            srcs[i].src_ref = srcs[duplicates[i]].src_ref.clone();
        }
    }

    ssa_used.clear();
}

impl Shader<'_> {
    pub fn legalize(&mut self) {
        let model = self.model;
        let mut ssa_used = SSAValueSet::new();
        self.map_instrs(|mut instr, ssa_alloc| {
            let mut b = SSAInstrBuilder::new(model, ssa_alloc);
            legalize_vec_srcs(&mut b, &mut instr, &mut ssa_used);
            for src_idx in 0..instr.srcs().len() {
                legalize_imm_src(&mut b, &mut instr, src_idx)
            }
            b.push_instr(instr);
            b.into_mapped()
        });
    }
}
