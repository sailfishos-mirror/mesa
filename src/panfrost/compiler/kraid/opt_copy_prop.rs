// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::ops::*;
use rustc_hash::FxHashMap;

struct WordCopy {
    src_type: DataType,
    src: Src,
}

impl WordCopy {
    fn as_ssa(&self, bytes: u8) -> Option<SSAValue> {
        let vec = self.src.src_ref.as_ssa()?;
        debug_assert!(vec.comps() == 1);
        let ssa = vec[0];

        if ssa.bytes() == bytes
            && self.src.src_mod.is_none()
            && self.src.swizzle.replicates_scalar(ssa.bits())
        {
            Some(ssa)
        } else {
            None
        }
    }
}

struct WordCopies<'a> {
    model: &'a dyn Model,
    copies: FxHashMap<SSAValue, WordCopy>,
}

impl WordCopies<'_> {
    fn new(model: &dyn Model) -> WordCopies<'_> {
        WordCopies {
            model,
            copies: Default::default(),
        }
    }

    fn add_copy(&mut self, ssa: SSAValue, src: Src, src_type: DataType) {
        assert!(!matches!(src.src_ref, SrcRef::Reg(_)));
        self.copies.insert(ssa, WordCopy { src_type, src });
    }

    fn try_add_instr(&mut self, instr: &Instr) {
        match &instr.op {
            Op::Copy(op) => {
                debug_assert!(op.src.src_mod.is_none());
                if let DstRef::SSA(vec) = &op.dst.dst_ref {
                    for w in 0..vec.comps() {
                        let ssa = vec[usize::from(w)];
                        self.add_copy(
                            ssa,
                            op.src.clone().word(w),
                            DataType::i(ssa.bits()),
                        );
                    }
                }
            }
            Op::FAdd(op) => {
                if let DstRef::SSA(vec) = &op.dst.dst_ref {
                    debug_assert_eq!(vec.comps(), 1);
                    let ssa = vec[0];

                    if op.round == FRound::NearestEven
                        && op.clamp == FClamp::None
                    {
                        if op.srcs[0].is_fneg_zero(op.dst_type) {
                            self.add_copy(ssa, op.srcs[1].clone(), op.dst_type);
                        }
                        if op.srcs[1].is_fneg_zero(op.dst_type) {
                            self.add_copy(ssa, op.srcs[0].clone(), op.dst_type);
                        }
                    }
                }
            }
            Op::ShiftLop(op) => {
                if let DstRef::SSA(vec) = &op.dst.dst_ref {
                    debug_assert_eq!(vec.comps(), 1);
                    let ssa = vec[0];

                    if op.shift_op == ShiftOp::None
                        && op.logic_op == LogicOp::None
                    {
                        let mut src = op.src0.clone();
                        if op.not_result {
                            src = src.bnot();
                        }
                        self.add_copy(ssa, src, op.dst_type);
                    }
                }
            }
            Op::Swz(op) => {
                debug_assert!(op.src.src_mod.is_none());
                if let DstRef::SSA(vec) = &op.dst.dst_ref {
                    for w in 0..vec.comps() {
                        let ssa = vec[usize::from(w)];
                        self.add_copy(ssa, op.src.clone().word(w), op.src_type);
                    }
                }
            }
            _ => (),
        }
    }

    fn try_prop_to_ssa_src(&self, src: &mut Src) {
        debug_assert!(src.src_mod.is_none());
        let SrcRef::SSA(src_vec) = &mut src.src_ref else {
            return;
        };

        for src_ssa in src_vec {
            if let Some(copy_ssa) = self
                .copies
                .get(src_ssa)
                .and_then(|copy| copy.as_ssa(src_ssa.bytes()))
            {
                *src_ssa = copy_ssa;
            }
        }
    }

    fn try_prop_to_src32(&self, instr: &mut Instr, src_idx: usize) {
        let src = &instr.srcs()[src_idx];
        let src_type = instr.src_type(src);

        let SrcRef::SSA(src_vec) = &src.src_ref else {
            return;
        };
        assert!(src_vec.comps() == 1);
        let src_ssa = src_vec[0];

        let Some(copy) = self.copies.get(&src_ssa) else {
            return;
        };

        // Short-cut if it happens to be an SSA copy
        if let Some(copy_ssa) = copy.as_ssa(src_ssa.bytes()) {
            instr.srcs_mut()[src_idx].src_ref = copy_ssa.into();
            return;
        };

        match copy.src.src_mod {
            SrcMod::None => (),
            SrcMod::BNot => {
                // BNot requires an integer type
                if !src_type.is_int_type() {
                    return;
                }
            }
            SrcMod::FAbs | SrcMod::FNeg | SrcMod::FNegAbs => {
                // If we have a float source modifier sitting between the two
                // swizzles, we need to ensure that src.swizzle respects it so
                // that we can re-order the copy source modifier and the
                // instruction's swizzle.
                match src_type {
                    DataType::F32 => {
                        if !src.swizzle.is_none() {
                            return;
                        }
                    }
                    DataType::F16 | DataType::V2F16 => {
                        if !matches!(
                            src.swizzle,
                            Swizzle::H00
                                | Swizzle::H01
                                | Swizzle::H10
                                | Swizzle::H11
                        ) {
                            return;
                        }
                    }
                    _ => {
                        debug_assert!(!src_type.is_float_type());
                        return;
                    }
                }
            }
        }

        let src_mod = copy.src.src_mod.modify(src.src_mod);
        let Some(swizzle) = copy.src.swizzle.swizzle(src.swizzle) else {
            return;
        };
        let new_src = Src {
            src_ref: copy.src.src_ref.clone(),
            swizzle,
            src_mod,
            last_use: false,
        };

        if self.model.op_src_supports_swizzle(&instr.op, src, swizzle)
            && self.model.op_src_supports_mod(&instr.op, src, src_mod)
        {
            instr.srcs_mut()[src_idx] = new_src;
        }
    }

    fn try_prop_to_src64(&self, instr: &mut Instr, src_idx: usize) {
        // TODO: Do better for 64-bit
        let src = &mut instr.srcs_mut()[src_idx];
        self.try_prop_to_ssa_src(src);
    }

    fn try_prop_to_src(&self, instr: &mut Instr, src_idx: usize) {
        let src = &instr.srcs()[src_idx];
        let src_type = instr.src_type(src);
        let is_sr = self.model.op_src_is_staging_reg(&instr.op, src);

        if is_sr {
            let src = &mut instr.srcs_mut()[src_idx];
            self.try_prop_to_ssa_src(src);
        } else if src_type.bits() == 64 {
            self.try_prop_to_src64(instr, src_idx);
        } else {
            self.try_prop_to_src32(instr, src_idx);
        }
    }
}

fn copy_prop_words(s: &mut Shader) {
    let mut copies = WordCopies::new(s.model);

    for block in s.blocks.iter_mut() {
        for instr in block.instrs.iter_mut() {
            for src_idx in 0..instr.srcs().len() {
                copies.try_prop_to_src(instr, src_idx);
            }
            copies.try_add_instr(instr);
        }
    }
}

impl Shader<'_> {
    pub fn opt_copy_prop(&mut self) {
        copy_prop_words(self);
    }
}
