// Copyright © 2026 Arm Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use kraid_bindings::*;
use rustc_hash::FxHashMap;
use std::num::NonZeroU32;

#[derive(Debug)]
struct FauConst {
    imm: NonZeroU32,
    weight: u32,
    fau_idx: Option<u16>,
}

fn fau_available(fau: &pan_fau_layout) -> usize {
    unsafe { pan_fau_available(fau) as usize }
}

fn fau_emit_const(fau: &mut pan_fau_layout, imm: u32) -> u16 {
    unsafe { pan_fau_emit_const(fau, imm) }.try_into().unwrap()
}

fn promote_consts(s: &mut Shader, fau: &mut pan_fau_layout) {
    let mut fau_consts = FxHashMap::default();

    // TODO: Take into account control flow and try to use source mods to unify
    // constants together.
    for block in s.blocks.iter() {
        for instr in block.instrs.iter() {
            for src in instr.srcs() {
                if let SrcRef::Imm32(v) = &src.src_ref {
                    fau_consts
                        .entry(*v)
                        .and_modify(|fc: &mut FauConst| fc.weight += 1)
                        .or_insert(FauConst {
                            imm: *v,
                            weight: 1,
                            fau_idx: None,
                        });
                }
            }
        }
    }

    let mut weighted: Vec<&mut FauConst> = fau_consts.values_mut().collect();
    weighted.sort_by(|a, b| a.weight.cmp(&b.weight).reverse());

    for fc in weighted {
        if fau_available(fau) == 0 {
            break;
        }

        fc.fau_idx = Some(fau_emit_const(fau, u32::from(fc.imm)));
    }

    for block in s.blocks.iter_mut() {
        for instr in block.instrs.iter_mut() {
            for src in instr.srcs_mut() {
                if let SrcRef::Imm32(v) = &src.src_ref {
                    let entry = fau_consts.get(v).unwrap();
                    if let Some(idx) = entry.fau_idx {
                        src.src_ref = SrcRef::FAU(FAURef::user_i32(idx));
                    }
                }
            }
        }
    }
}

impl Shader<'_> {
    pub fn opt_promote_consts(&mut self, fau: &mut pan_fau_layout) {
        promote_consts(self, fau);
    }
}
