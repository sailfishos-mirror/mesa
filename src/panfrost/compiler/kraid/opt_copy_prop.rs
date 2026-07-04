// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::ops::*;
use crate::swizzle::*;
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

#[derive(Clone, Copy, PartialEq)]
enum ByteRef {
    Imm8(u8),
    FAU(FAURef),
    SSA(SSAValue),
}

#[derive(Clone, Copy)]
struct ByteCopy {
    byte_ref: ByteRef,
    swiz_byte: SwizzleByte,
}

impl ByteCopy {
    const ZERO: ByteCopy = ByteCopy {
        byte_ref: ByteRef::Imm8(0),
        swiz_byte: SwizzleByte::Zero,
    };

    fn from_src(src: &Src, byte: u8) -> ByteCopy {
        match &src.src_ref {
            SrcRef::Zero => ByteCopy::ZERO,
            SrcRef::Imm32(imm) => {
                let imm32 = src.swizzle.fold_u32(imm.get()).unwrap();
                ByteCopy {
                    byte_ref: ByteRef::Imm8((imm32 >> (byte * 8)) as u8),
                    swiz_byte: SwizzleByte::Byte0,
                }
            }
            SrcRef::FAU(fau) => {
                let swiz_byte = src.swizzle.byte(byte).unwrap();
                debug_assert!(!swiz_byte.is_fext());
                if swiz_byte == SwizzleByte::Zero {
                    ByteCopy::ZERO
                } else {
                    ByteCopy {
                        byte_ref: ByteRef::FAU(*fau),
                        swiz_byte,
                    }
                }
            }
            SrcRef::SSA(vec) => {
                debug_assert_eq!(vec.comps(), 1);
                let swiz_byte = src.swizzle.byte(byte).unwrap();
                debug_assert!(!swiz_byte.is_fext());
                if swiz_byte == SwizzleByte::Zero {
                    ByteCopy::ZERO
                } else {
                    ByteCopy {
                        byte_ref: ByteRef::SSA(vec[0]),
                        swiz_byte,
                    }
                }
            }
            SrcRef::Reg(_) => {
                panic!("Must be run in SSA form");
            }
        }
    }
}

struct PerByteWordCopy {
    bytes: [ByteCopy; 4],
}

impl PerByteWordCopy {
    fn new(bytes: [ByteCopy; 4]) -> PerByteWordCopy {
        PerByteWordCopy { bytes }
    }

    fn as_imm32(&self, byte_mask: u8) -> Option<u32> {
        if byte_mask == 0 {
            return Some(0);
        }

        debug_assert!(byte_mask < (1 << 4));
        let mut imm32 = 0_u32;
        for b in 0..4 {
            if ((byte_mask >> b) & 1) != 0 {
                let byte = self.bytes[b];
                if byte.swiz_byte == SwizzleByte::Zero {
                    continue;
                }
                if let ByteRef::Imm8(imm8) = byte.byte_ref {
                    debug_assert!(byte.swiz_byte == SwizzleByte::Byte0);
                    imm32 |= u32::from(imm8) << (8 * b);
                } else {
                    return None;
                }
            }
        }
        Some(imm32)
    }

    fn as_ssa(&self, bytes: u8) -> Option<SSAValue> {
        let ByteRef::SSA(ssa) = self.bytes[0].byte_ref else {
            return None;
        };
        if ssa.bytes() != bytes {
            return None;
        }
        for b in 0..bytes {
            let byte = &self.bytes[usize::from(b)];
            if byte.byte_ref != ByteRef::SSA(ssa)
                || byte.swiz_byte != SwizzleByte::byte(b)
            {
                return None;
            }
        }
        Some(ssa)
    }

    fn as_src(&self, byte_mask: u8) -> Option<Src> {
        if let Some(imm32) = self.as_imm32(byte_mask) {
            return Some(imm32.into());
        }

        let mut byte_ref = ByteRef::Imm8(0);
        let mut swiz_bytes = [SwizzleByte::Zero; 4];
        for b in 0..4 {
            if ((byte_mask >> b) & 1) != 0 {
                let byte = self.bytes[b];
                match byte.byte_ref {
                    ByteRef::Imm8(imm8) => {
                        if imm8 != 0 {
                            // We can't handle partial immediates
                            return None;
                        }
                        // swiz_bytes is already initialized to Zero.
                    }
                    br => {
                        if byte_ref == ByteRef::Imm8(0) {
                            byte_ref = br;
                        } else if byte_ref != br {
                            return None;
                        }
                        debug_assert!(byte.swiz_byte != SwizzleByte::Zero);
                        swiz_bytes[b] = byte.swiz_byte;
                    }
                }
            }
        }

        let swizzle = Swizzle::from_swizzle_bytes(swiz_bytes)?;
        let src_ref = match byte_ref {
            ByteRef::Imm8(_) => {
                // This can't happen because we would have caught it in the
                // all immediates case.
                unreachable!();
            }
            ByteRef::FAU(fau) => SrcRef::FAU(fau),
            ByteRef::SSA(ssa) => SrcRef::SSA(ssa.into()),
        };
        Some(Src {
            src_ref,
            src_mod: SrcMod::None,
            swizzle,
            last_use: false,
        })
    }
}

struct ByteCopies<'a> {
    model: &'a dyn Model,
    copies: FxHashMap<SSAValue, PerByteWordCopy>,
}

impl ByteCopies<'_> {
    fn new(model: &dyn Model) -> ByteCopies<'_> {
        ByteCopies {
            model,
            copies: Default::default(),
        }
    }

    fn chase_byte(&self, mut byte: ByteCopy) -> ByteCopy {
        loop {
            let ByteRef::SSA(ssa) = &byte.byte_ref else {
                return byte;
            };

            let Some(copy) = self.copies.get(&ssa) else {
                return byte;
            };

            // We've already ensured that SwizzleByte::Zero is never combined
            // with ByteRef::SSA or ByteRef::FAU
            let byte_idx = byte.swiz_byte.byte_idx().unwrap();
            let byte_mod = byte.swiz_byte.byte_mod().unwrap();

            let copy_byte = &copy.bytes[usize::from(byte_idx)];
            let Some(swiz_byte) = copy_byte.swiz_byte.modify(byte_mod) else {
                return byte;
            };
            if swiz_byte == SwizzleByte::Zero {
                return ByteCopy::ZERO;
            }

            byte = ByteCopy {
                byte_ref: copy_byte.byte_ref,
                swiz_byte,
            }
        }
    }

    fn chase_src_byte(&self, src: &Src, byte: u8) -> ByteCopy {
        self.chase_byte(ByteCopy::from_src(&src, byte))
    }

    fn add_word_copy(&mut self, ssa: SSAValue, src: Src) {
        debug_assert!(src.src_mod.is_none());
        let mut bytes = [ByteCopy::ZERO; 4];
        for b in 0..ssa.bytes() {
            bytes[usize::from(b)] = self.chase_src_byte(&src, b);
        }
        self.copies.insert(ssa, PerByteWordCopy::new(bytes));
    }

    fn try_add_instr(&mut self, instr: &Instr) {
        match &instr.op {
            Op::Copy(op) => {
                debug_assert!(op.src.src_mod.is_none());
                if let DstRef::SSA(vec) = &op.dst.dst_ref {
                    for w in 0..vec.comps() {
                        let ssa = vec[usize::from(w)];
                        self.add_word_copy(ssa, op.src.clone().word(w));
                    }
                }
            }
            Op::MkVecV2I16(op) => {
                if let DstRef::SSA(vec) = &op.dst.dst_ref {
                    let bytes = [
                        self.chase_src_byte(&op.srcs[0], 0),
                        self.chase_src_byte(&op.srcs[0], 1),
                        self.chase_src_byte(&op.srcs[1], 0),
                        self.chase_src_byte(&op.srcs[1], 1),
                    ];
                    debug_assert!(vec.comps() == 1);
                    self.copies.insert(vec[0], PerByteWordCopy::new(bytes));
                }
            }
            Op::MkVecV4I8(op) => {
                if let DstRef::SSA(vec) = &op.dst.dst_ref {
                    let bytes = [
                        self.chase_src_byte(&op.srcs[0], 0),
                        self.chase_src_byte(&op.srcs[1], 0),
                        self.chase_src_byte(&op.srcs[2], 0),
                        self.chase_src_byte(&op.srcs[3], 0),
                    ];
                    debug_assert!(vec.comps() == 1);
                    self.copies.insert(vec[0], PerByteWordCopy::new(bytes));
                }
            }
            Op::Swz(op) => {
                debug_assert!(op.src.src_mod.is_none());
                if !op.src.swizzle.is_f16_widen() {
                    if let DstRef::SSA(vec) = &op.dst.dst_ref {
                        for w in 0..vec.comps() {
                            let ssa = vec[usize::from(w)];
                            self.add_word_copy(ssa, op.src.clone().word(w));
                        }
                    }
                }
            }
            _ => (),
        }
    }

    fn try_prop_to_ssa_ref(&self, src_vec: &mut SSARef) {
        for src_ssa in src_vec {
            if let Some(copy) = self.copies.get(src_ssa) {
                if let Some(ssa) = copy.as_ssa(src_ssa.bytes()) {
                    *src_ssa = ssa;
                }
            }
        }
    }

    fn try_prop_to_src(&self, instr: &mut Instr, src_idx: usize) {
        let src = &instr.srcs()[src_idx];
        let is_sr = self.model.op_src_is_staging_reg(&instr.op, src);
        let SrcRef::SSA(src_vec) = &src.src_ref else {
            return;
        };

        if is_sr || src_vec.comps() > 1 {
            let src = &mut instr.srcs_mut()[src_idx];
            self.try_prop_to_ssa_ref(src.src_ref.as_mut_ssa().unwrap());
            return;
        }

        let src_ssa = src_vec[0];
        let Some(copy) = self.copies.get(&src_ssa) else {
            return;
        };
        let Some(copy_src) = copy.as_src(src.swizzle.bytes_read(4)) else {
            return;
        };
        let Some(swizzle) = copy_src.swizzle.swizzle(src.swizzle) else {
            return;
        };

        if !self.model.op_src_supports_swizzle(&instr.op, src, swizzle) {
            return;
        }

        let src = &mut instr.srcs_mut()[src_idx];
        src.src_ref = copy_src.src_ref;
        src.swizzle = swizzle;
    }
}

fn copy_prop_bytes(s: &mut Shader) {
    let mut copies = ByteCopies::new(s.model);

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
        copy_prop_bytes(self);
    }
}
