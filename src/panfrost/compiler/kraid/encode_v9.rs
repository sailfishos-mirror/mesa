// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bitview::*;
use crate::data_type::*;
use crate::flow::*;
use crate::ir::*;
use crate::isa::v9::*;
use crate::isa::*;
use crate::ops::*;
use crate::swizzle::*;

use paste::paste;
use rustc_hash::FxHashMap;

fn encode_src_ref(src: &SrcRef, last_use: bool) -> u8 {
    match src {
        SrcRef::FAU(fau) => match fau.page {
            FAUPage::User => {
                // The top two bits are in the FAU page
                let idx = (fau.idx % 64) as u8;
                (0b10 << 6) | idx
            }
            FAUPage::Special0 | FAUPage::Special1 | FAUPage::Special3 => {
                assert!(fau.idx < 32);
                (0b111 << 5) | (fau.idx as u8)
            }
            FAUPage::SmallConst => {
                assert!(fau.idx < 32);
                (0b110 << 5) | (fau.idx as u8)
            }
        },
        SrcRef::Reg(reg) => {
            assert!(reg.idx < 32);
            ((last_use as u8) << 5) | reg.idx
        }
        _ => panic!("SSAValues and Immediates need to be lowered"),
    }
}

fn src_fau_page(src: &Src) -> Option<u8> {
    let SrcRef::FAU(fau) = &src.src_ref else {
        return None;
    };

    match fau.page {
        FAUPage::User => {
            let page = fau.idx / 64;
            assert!(page < 4);
            Some(page as u8)
        }
        FAUPage::Special0 => Some(0),
        FAUPage::Special1 => Some(1),
        FAUPage::Special3 => Some(3),
        FAUPage::SmallConst => None,
    }
}

fn op_encode_src(op: &impl Opcode, src: &Src) -> v9::EncodedSrc {
    let encoded = encode_src_ref(&src.src_ref, src.last_use);

    let src_type = op.src_type(src);
    if src_type.bits().unwrap().get() == 64 && !src.src_ref.is_small_const() {
        assert_eq!(encoded & 0x01, 0);
    }

    let swizzle_widen = AsmSwizzleWiden::from_swizzle(src_type, src.swizzle)
        .expect("Invalid swizzle");

    // Sanity check our source modifiers first
    match src.src_mod {
        SrcMod::None => (),
        SrcMod::FAbs | SrcMod::FNeg | SrcMod::FNegAbs => {
            assert_eq!(src_type.num_type(), Some(NumericType::Float));
        }
        SrcMod::BNot => {
            assert_eq!(src_type.num_type(), Some(NumericType::Integer));
        }
    }

    v9::EncodedSrc {
        encoded,
        swizzle: swizzle_widen.into(),
        abs: matches!(src.src_mod, SrcMod::FAbs | SrcMod::FNegAbs),
        neg: matches!(src.src_mod, SrcMod::FNeg | SrcMod::FNegAbs),
        not: matches!(src.src_mod, SrcMod::BNot),
    }
}

fn op_encode_dst(op: &impl Opcode, dst: &Dst) -> v9::EncodedDst {
    let Dst::Reg(reg) = dst else {
        panic!("Destination must be a register");
    };

    let dst_type = op.dst_type(dst);
    if dst_type.bits().unwrap().get() == 64 {
        assert_eq!(reg.idx & 0x01, 0);
    }

    let lanes = match reg.range {
        RegRange::Half0 => DstLanes::H0,
        RegRange::Half1 => DstLanes::H1,
        RegRange::Regs(_) => DstLanes::None,
    };

    v9::EncodedDst {
        reg: reg.idx,
        lanes,
    }
}

fn op_encode_sr_read(op: &impl Opcode, src: &Src) -> v9::SrRead {
    let SrcRef::Reg(reg) = &src.src_ref else {
        panic!("Staging registers be registers");
    };

    let src_type = op.src_type(src);
    let swizzle = AsmSwizzleWiden::from_swizzle(src_type, src.swizzle)
        .expect("Invalid swizzle");
    let swizzle = swizzle.into();

    assert!(src.src_mod == SrcMod::None);

    let index = reg.idx;
    let count = reg.bytes().div_ceil(4);

    assert_eq!(index % count.next_power_of_two(), 0);

    v9::SrRead {
        index,
        count,
        swizzle,
    }
}

fn op_encode_sr_write(op: &impl Opcode, dst: &Dst) -> v9::SrWrite {
    let Dst::Reg(reg) = dst else {
        panic!("Staging registers be registers");
    };

    let reg_bytes = reg.bytes();
    let reg_byte_offset = reg.byte_offset();

    let type_bits = op.dst_type(dst).total_bits().unwrap().get();
    let lanes = if type_bits == 8 {
        match reg_byte_offset {
            0 => DstLanes::B0,
            1 => DstLanes::B1,
            2 => DstLanes::B2,
            3 => DstLanes::B3,
            b => panic!("Invalid byte offset: {b}"),
        }
    } else if type_bits == 16 {
        assert!(reg_bytes >= 2);
        match reg_byte_offset {
            0 => DstLanes::H0,
            2 => DstLanes::H1,
            b => panic!("Invalid byte offset: {b}"),
        }
    } else {
        assert!(reg_bytes * 8 >= type_bits);
        assert_eq!(reg_byte_offset, 0);
        DstLanes::None
    };

    let index = reg.idx;
    let count = reg_bytes.div_ceil(4);

    v9::SrWrite {
        index,
        count,
        lanes,
    }
}

fn instr_fau_page(instr: &Instr) -> Option<u8> {
    let mut page = None;
    for src in instr.srcs() {
        let Some(p) = src_fau_page(src) else {
            continue;
        };

        let page = page.get_or_insert(p);
        assert!(*page == p);
    }
    page
}

fn encode_flow(mut flow: FlowCtrl, arch: u8) -> FlowControlM {
    if flow.take_end_shader() {
        assert!(flow == FlowCtrl::NONE);
        return FlowControlM::End;
    }

    if flow.take_discard() {
        assert!(flow == FlowCtrl::NONE);
        return FlowControlM::Discard;
    }

    if flow.take_wait_bit(FlowWaitBit::Barrier) {
        return FlowControlM::Wait;
    }

    if arch >= 10 && flow.take_wait_bit(FlowWaitBit::Resource) {
        if flow == FlowCtrl::NONE {
            return FlowControlM::WaitResource;
        } else {
            return FlowControlM::Wait;
        }
    }

    if flow.take_reconverge() {
        if flow == FlowCtrl::NONE {
            return FlowControlM::Reconverge;
        } else {
            return FlowControlM::Wait;
        }
    }

    if flow.take_wait_bit(FlowWaitBit::ZS) {
        if (flow.wait & !0b111) == 0 {
            return FlowControlM::Wait0126;
        } else {
            return FlowControlM::Wait;
        }
    }

    assert!(flow.wait <= 0b111);
    unsafe { std::mem::transmute(flow.wait) }
}

impl From<AsmSwizzleWiden> for SrcSwizzle {
    fn from(swizzle: AsmSwizzleWiden) -> SrcSwizzle {
        match swizzle {
            AsmSwizzleWiden::B0 => SrcSwizzle::B0,
            AsmSwizzleWiden::B00 => SrcSwizzle::B00,
            AsmSwizzleWiden::B0000 => SrcSwizzle::B0000,
            AsmSwizzleWiden::B0011 => SrcSwizzle::B0011,
            AsmSwizzleWiden::B01 => SrcSwizzle::B01,
            AsmSwizzleWiden::B0101 => SrcSwizzle::B0101,
            AsmSwizzleWiden::B0123 => SrcSwizzle::B0123,
            AsmSwizzleWiden::B02 => SrcSwizzle::B02,
            AsmSwizzleWiden::B03 => SrcSwizzle::B03,
            AsmSwizzleWiden::B1 => SrcSwizzle::B1,
            AsmSwizzleWiden::B10 => SrcSwizzle::B10,
            AsmSwizzleWiden::B1032 => SrcSwizzle::B1032,
            AsmSwizzleWiden::B11 => SrcSwizzle::B11,
            AsmSwizzleWiden::B1111 => SrcSwizzle::B1111,
            AsmSwizzleWiden::B12 => SrcSwizzle::B12,
            AsmSwizzleWiden::B13 => SrcSwizzle::B13,
            AsmSwizzleWiden::B2 => SrcSwizzle::B2,
            AsmSwizzleWiden::B20 => SrcSwizzle::B20,
            AsmSwizzleWiden::B21 => SrcSwizzle::B21,
            AsmSwizzleWiden::B22 => SrcSwizzle::B22,
            AsmSwizzleWiden::B2222 => SrcSwizzle::B2222,
            AsmSwizzleWiden::B2233 => SrcSwizzle::B2233,
            AsmSwizzleWiden::B23 => SrcSwizzle::B23,
            AsmSwizzleWiden::B2301 => SrcSwizzle::B2301,
            AsmSwizzleWiden::B2323 => SrcSwizzle::B2323,
            AsmSwizzleWiden::B3 => SrcSwizzle::B3,
            AsmSwizzleWiden::B30 => SrcSwizzle::B30,
            AsmSwizzleWiden::B31 => SrcSwizzle::B31,
            AsmSwizzleWiden::B32 => SrcSwizzle::B32,
            AsmSwizzleWiden::B3210 => SrcSwizzle::B3210,
            AsmSwizzleWiden::B33 => SrcSwizzle::B33,
            AsmSwizzleWiden::B3333 => SrcSwizzle::B3333,
            AsmSwizzleWiden::H0 => SrcSwizzle::H0,
            AsmSwizzleWiden::H00 => SrcSwizzle::H00,
            AsmSwizzleWiden::H01 => SrcSwizzle::H01,
            AsmSwizzleWiden::H1 => SrcSwizzle::H1,
            AsmSwizzleWiden::H10 => SrcSwizzle::H10,
            AsmSwizzleWiden::H11 => SrcSwizzle::H11,
            AsmSwizzleWiden::None => SrcSwizzle::None,
            AsmSwizzleWiden::W0 => SrcSwizzle::W0,
            AsmSwizzleWiden::W1 => SrcSwizzle::W1,
        }
    }
}

impl From<MemAccess> for AccessLoadM {
    fn from(access: MemAccess) -> AccessLoadM {
        match access {
            MemAccess::None => AccessLoadM::None,
            MemAccess::Istream => AccessLoadM::Istream,
            MemAccess::Estream => AccessLoadM::Estream,
            MemAccess::Force => AccessLoadM::Force,
        }
    }
}

impl From<MemAccess> for AccessStoreM {
    fn from(access: MemAccess) -> AccessStoreM {
        match access {
            MemAccess::None => AccessStoreM::None,
            MemAccess::Istream => AccessStoreM::Istream,
            MemAccess::Estream => AccessStoreM::Estream,
            MemAccess::Force => AccessStoreM::Force,
        }
    }
}

macro_rules! shift_lop_as {
    ($op:expr, $Instr:ident) => {
        paste! {
            $Instr {
                variant: match $op.dst_type {
                    DataType::I32 => [<$Instr Variant>]::I32,
                    DataType::I64 => [<$Instr Variant>]::I64,
                    DataType::V2I16 => [<$Instr Variant>]::V2i16,
                    DataType::V4I8 => [<$Instr Variant>]::V4i8,
                    t => panic!("SHIFT_LOP.{t} not supported"),
                },
                dst: op_encode_dst($op, &$op.dst),
                not_result: $op.not_result.into(),
                src0: op_encode_src($op, &$op.src0),
                src1: op_encode_src($op, &$op.shift),
                src2: op_encode_src($op, &$op.src2),
            }
        }
    };
}

struct InstrEnc {
    arch: u8,
    fau_page_index: u8,
    flow: FlowControlM,
}

impl InstrEnc {
    fn encode(
        self,
        instr: impl TryEncode<Encoded = [u32; 2], Error: std::fmt::Debug>,
    ) -> [u32; 2] {
        let mut bits = instr
            .try_encode(self.arch)
            .expect("Failed to encode instruction");
        let flow = self
            .flow
            .try_encode(self.arch)
            .expect("Failed to encode flow");

        let mut b = BitMutView::new(&mut bits);
        b.set_field(57..59, self.fau_page_index);
        b.set_field(59..63, flow);

        bits
    }
}

fn encode_instr(
    ip: i64,
    instr: &Instr,
    arch: u8,
    labels: &FxHashMap<Label, i64>,
) -> [u32; 2] {
    let msg_slot_idx = instr.flow.get_msg_slot_idx().map(|idx| match idx {
        0 => MessageSlotIndexM::Slot0,
        1 => MessageSlotIndexM::Slot1,
        2 => MessageSlotIndexM::Slot2,
        _ => panic!("Invalid message slot"),
    });

    let enc = InstrEnc {
        arch,
        fau_page_index: instr_fau_page(instr).unwrap_or(0),
        flow: encode_flow(instr.flow, arch),
    };

    match &instr.op {
        Op::Branch(op) => enc.encode(Branch {
            not: op.not.into(),
            src0: op_encode_src(op, &op.cond),
            combine: match op.combine_op {
                BranchCombineOp::None => BranchCombineM::And,
                BranchCombineOp::H0 => BranchCombineM::H0,
                BranchCombineOp::H1 => BranchCombineM::H1,
                BranchCombineOp::And => BranchCombineM::Lowbits,
                BranchCombineOp::LowBits => BranchCombineM::None,
            },
            conservative: false.into(),
            offset: labels.get(&op.label).unwrap() - (ip + INSTR_SIZE),
        }),
        Op::FAdd(op) => {
            if let SrcRef::Imm32(imm) = &op.srcs[1].src_ref {
                enc.encode(FaddImm {
                    variant: match op.dst_type {
                        DataType::F32 => FaddImmVariant::F32,
                        DataType::V2F16 => FaddImmVariant::V2f16,
                        t => panic!("FADD_IMM.{t} not supported"),
                    },
                    dst: op_encode_dst(op, &op.dst),
                    src0: op_encode_src(op, &op.srcs[0]),
                    imm1w: *imm,
                })
            } else {
                enc.encode(Fadd {
                    variant: match op.dst_type {
                        DataType::F32 => FaddVariant::F32,
                        DataType::V2F16 => FaddVariant::V2f16,
                        t => panic!("FADD.{t} not supported"),
                    },
                    dst: op_encode_dst(op, &op.dst),
                    src0: op_encode_src(op, &op.srcs[0]),
                    src1: op_encode_src(op, &op.srcs[1]),
                    clamp: ClampM::None,
                    round: Round::None,
                    sticky: false.into(),
                })
            }
        }
        Op::FCmp(op) => enc.encode(Fcmp {
            variant: match op.src_type {
                DataType::F32 => FcmpVariant::F32,
                DataType::V2F16 => FcmpVariant::V2f16,
                t => panic!("FCMP.{t} not supported"),
            },
            dst: op_encode_dst(op, &op.dst),
            src0: op_encode_src(op, &op.srcs[0]),
            src1: op_encode_src(op, &op.srcs[1]),
            cmpf: match op.cmp_op {
                CmpOp::Eq => CmpfM::Eq,
                CmpOp::Gt => CmpfM::Gt,
                CmpOp::Ge => CmpfM::Ge,
                CmpOp::Ne => CmpfM::Ne,
                CmpOp::Lt => CmpfM::Lt,
                CmpOp::Le => CmpfM::Le,
                CmpOp::GtLt => CmpfM::Gtlt,
                CmpOp::Total => CmpfM::Total,
            },
            result_type: CmpResultTypeM::M1,
        }),
        Op::IAdd(op) => {
            if let SrcRef::Imm32(imm) = &op.srcs[1].src_ref {
                enc.encode(IaddImm {
                    variant: match op.dst_type {
                        DataType::I32 => IaddImmVariant::I32,
                        DataType::V2I16 => IaddImmVariant::V2i16,
                        DataType::V4I8 => IaddImmVariant::V4i8,
                        t => panic!("IADD_IMM.{t} not supported"),
                    },
                    dst: op_encode_dst(op, &op.dst),
                    src0: op_encode_src(op, &op.srcs[0]),
                    imm1w: *imm,
                })
            } else {
                enc.encode(Iadd {
                    variant: match op.dst_type {
                        DataType::S32 => IaddVariant::S32,
                        DataType::S64 => IaddVariant::S64,
                        DataType::U32 => IaddVariant::U32,
                        DataType::U64 => IaddVariant::U64,
                        DataType::V2S16 => IaddVariant::V2s16,
                        DataType::V2U16 => IaddVariant::V2u16,
                        DataType::V4S8 => IaddVariant::V4s8,
                        DataType::V4U8 => IaddVariant::V4u8,
                        t => panic!("IADD.{t} not supported"),
                    },
                    dst: op_encode_dst(op, &op.dst),
                    src0: op_encode_src(op, &op.srcs[0]),
                    src1: op_encode_src(op, &op.srcs[1]),
                    saturate: op.saturate.into(),
                })
            }
        }
        Op::ICmp(op) => enc.encode(Icmp {
            variant: match op.src_type {
                DataType::S32 => IcmpVariant::S32,
                DataType::U32 => IcmpVariant::U32,
                DataType::V2S16 => IcmpVariant::V2s16,
                DataType::V2U16 => IcmpVariant::V2u16,
                t => panic!("FCMP.{t} not supported"),
            },
            dst: op_encode_dst(op, &op.dst),
            src0: op_encode_src(op, &op.srcs[0]),
            src1: op_encode_src(op, &op.srcs[1]),
            cmpf: match op.cmp_op {
                CmpOp::Eq => CmpfM::Eq,
                CmpOp::Gt => CmpfM::Gt,
                CmpOp::Ge => CmpfM::Ge,
                CmpOp::Ne => CmpfM::Ne,
                CmpOp::Lt => CmpfM::Lt,
                CmpOp::Le => CmpfM::Le,
                cmp_op => panic!("Unsupported comparison: {cmp_op}"),
            },
            result_type: CmpResultTypeM::M1,
        }),
        Op::LdPka(op) => enc.encode(LdPka {
            variant: match op.dst_type {
                DataType::I8 => LdPkaVariant::I8,
                DataType::I16 => LdPkaVariant::I16,
                DataType::I24 => LdPkaVariant::I24,
                DataType::I32 => LdPkaVariant::I32,
                DataType::I48 => LdPkaVariant::I48,
                DataType::I64 => LdPkaVariant::I64,
                DataType::I96 => LdPkaVariant::I96,
                DataType::I128 => LdPkaVariant::I128,
                t => panic!("LD_PKA.{t} not supported"),
            },
            access: op.access.into(),
            extend: SignExtendOrNoneM::None,
            message_slot_index: msg_slot_idx.unwrap(),
            sr_dst: op_encode_sr_write(op, &op.dst),
            src0: op_encode_src(op, &op.offset),
            src1: op_encode_src(op, &op.handle),
        }),
        Op::LeaPka(op) => enc.encode(LeaPka {
            message_slot_index: msg_slot_idx.unwrap(),
            sr_dst: op_encode_sr_write(op, &op.dst),
            src0: op_encode_src(op, &op.offset),
            src1: op_encode_src(op, &op.handle),
        }),
        Op::Load(op) => enc.encode(Load {
            variant: match op.dst_type {
                DataType::I8 => LoadVariant::I8,
                DataType::I16 => LoadVariant::I16,
                DataType::I24 => LoadVariant::I24,
                DataType::I32 => LoadVariant::I32,
                DataType::I48 => LoadVariant::I48,
                DataType::I64 => LoadVariant::I64,
                DataType::I96 => LoadVariant::I96,
                DataType::I128 => LoadVariant::I128,
                t => panic!("LOAD.{t} not supported"),
            },
            access: op.access.into(),
            extend: SignExtendOrNoneM::None,
            message_slot_index: msg_slot_idx.unwrap(),
            sr_dst: op_encode_sr_write(op, &op.dst),
            src0: op_encode_src(op, &op.addr),
            offset: op.offset,
        }),
        Op::Mov(op) => {
            if let SrcRef::Imm32(imm) = &op.src.src_ref {
                enc.encode(MovImm {
                    variant: match op.dst_type {
                        DataType::I16 => MovImmVariant::V2i16,
                        DataType::I32 => MovImmVariant::I32,
                        t => panic!("MOV_IMM.{t} not supported"),
                    },
                    dst: op_encode_dst(op, &op.dst),
                    imm1w: *imm,
                })
            } else {
                enc.encode(Mov {
                    variant: match op.dst_type {
                        DataType::I32 => MovVariant::I32,
                        t => panic!("MOV.{t} not supported"),
                    },
                    dst: op_encode_dst(op, &op.dst),
                    src0: op_encode_src(op, &op.src),
                })
            }
        }
        Op::Nop(_) => enc.encode(Nop {}),
        Op::ShiftLop(op) => {
            use LogicOp::*;
            use ShiftOp::*;
            match (op.shift_op, op.logic_op) {
                (LShift, And) => enc.encode(shift_lop_as!(op, LshiftAnd)),
                (LShift, Or) => enc.encode(shift_lop_as!(op, LshiftOr)),
                (LShift, Xor) => enc.encode(shift_lop_as!(op, LshiftXor)),
                (RShift, And) => enc.encode(shift_lop_as!(op, RshiftAnd)),
                (RShift, Or) => enc.encode(shift_lop_as!(op, RshiftOr)),
                (RShift, Xor) => enc.encode(shift_lop_as!(op, RshiftXor)),
                (ARShift, _) => {
                    assert!(op.shift.is_zero());
                    enc.encode(Arshift {
                        variant: match op.dst_type {
                            DataType::I32 => ArshiftVariant::I32,
                            DataType::I64 => ArshiftVariant::I64,
                            DataType::V2I16 => ArshiftVariant::V2i16,
                            DataType::V4I8 => ArshiftVariant::V4i8,
                            t => panic!("SHIFT_LOP.{t} not supported"),
                        },
                        dst: op_encode_dst(op, &op.dst),
                        not_result: op.not_result.into(),
                        src0: op_encode_src(op, &op.src0),
                        src1: op_encode_src(op, &op.shift),
                    })
                }
                _ => todo!("Implement [lr]rot"),
            }
        }
        Op::Store(op) => enc.encode(Store {
            variant: match op.src_type {
                DataType::I8 => StoreVariant::I8,
                DataType::I16 => StoreVariant::I16,
                DataType::I24 => StoreVariant::I24,
                DataType::I32 => StoreVariant::I32,
                DataType::I48 => StoreVariant::I48,
                DataType::I64 => StoreVariant::I64,
                DataType::I96 => StoreVariant::I96,
                DataType::I128 => StoreVariant::I128,
                t => panic!("LOAD.{t} not supported"),
            },
            access: op.access.into(),
            message_slot_index: msg_slot_idx.unwrap(),
            sr_src: op_encode_sr_read(op, &op.data),
            src0: op_encode_src(op, &op.addr),
            offset: op.offset,
        }),
        op => panic!("Instruction not encoded: {op}"),
    }
}

const INSTR_SIZE: i64 = size_of::<<Mov as TryEncode>::Encoded>() as i64;

pub fn encode_v9(s: &Shader<'_>, arch: u8) -> Vec<u32> {
    let mut labels = FxHashMap::default();
    let mut ip = 0_i64;
    for b in &s.blocks {
        labels.insert(b.label, ip);
        ip += i64::try_from(b.instrs.len()).unwrap() * INSTR_SIZE;
    }

    let mut enc = Vec::new();
    let mut ip = 0_i64;
    for b in &s.blocks {
        for i in &b.instrs {
            enc.extend(encode_instr(ip, i, arch, &labels));
            ip += INSTR_SIZE;
        }
    }
    enc
}
