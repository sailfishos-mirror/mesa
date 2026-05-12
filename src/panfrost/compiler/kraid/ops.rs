// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use kraid_proc::{variants, FromVariants, Opcode};
use std::fmt;

macro_rules! bool_as_mod_str {
    ($s: expr, $mod: ident) => {
        if $s.$mod { stringify!(.$mod) } else { "" }
    }
}

#[derive(Clone, Copy, Default, Eq, Hash, PartialEq)]
pub enum BranchCombineOp {
    #[default]
    None,
    H0,
    H1,
    And,
    LowBits,
}

impl fmt::Display for BranchCombineOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BranchCombineOp::None => Ok(()),
            BranchCombineOp::H0 => write!(f, ".h0"),
            BranchCombineOp::H1 => write!(f, ".h1"),
            BranchCombineOp::And => write!(f, ".and"),
            BranchCombineOp::LowBits => write!(f, ".lowbits"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpBranch {
    pub not: bool,
    #[src_type(I32)]
    pub cond: Src,
    pub combine_op: BranchCombineOp,
    pub label: Label,
}

impl fmt::Display for OpBranch {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "BRANCH{} {}{} {}",
            bool_as_mod_str!(self, not),
            &self.cond,
            self.combine_op,
            self.label,
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpEnd {}

impl fmt::Display for OpEnd {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "END")
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, V2F16, F32])]
pub struct OpFAdd {
    pub dst: Dst,
    pub dst_type: DataType,
    pub srcs: [Src; 2],
}

impl fmt::Display for OpFAdd {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FADD.{} {} {}",
            &self.dst, &self.dst_type, &self.srcs[0], &self.srcs[1]
        )
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum CmpAccumOp {
    None,
    And,
    Or,
}

impl fmt::Display for CmpAccumOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CmpAccumOp::None => Ok(()),
            CmpAccumOp::And => write!(f, "_AND"),
            CmpAccumOp::Or => write!(f, "_OR"),
        }
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum CmpResultType {
    I1,
    F1,
    M1,
}

impl fmt::Display for CmpResultType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CmpResultType::I1 => write!(f, ".i1"),
            CmpResultType::F1 => write!(f, ".f1"),
            CmpResultType::M1 => write!(f, ".m1"),
        }
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum CmpOp {
    Eq,
    Gt,
    Ge,
    Ne,
    Lt,
    Le,
    GtLt,
    Total,
}

impl fmt::Display for CmpOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CmpOp::Eq => write!(f, ".eq"),
            CmpOp::Gt => write!(f, ".gt"),
            CmpOp::Ge => write!(f, ".ge"),
            CmpOp::Ne => write!(f, ".ne"),
            CmpOp::Lt => write!(f, ".lt"),
            CmpOp::Le => write!(f, ".le"),
            CmpOp::GtLt => write!(f, "gtlt"),
            CmpOp::Total => write!(f, ".total"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [F16, V2F16, F32])]
pub struct OpFCmp {
    pub dst: Dst,

    pub src_type: DataType,
    pub res_type: CmpResultType,
    pub cmp_op: CmpOp,

    pub srcs: [Src; 2],

    #[src_type(VNIN)]
    pub accum: Src,
    pub accum_op: CmpAccumOp,
}

impl fmt::Display for OpFCmp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FCMP{}.{}{}{} {} {}",
            &self.dst,
            self.accum_op,
            self.src_type,
            self.res_type,
            self.cmp_op,
            &self.srcs[0],
            &self.srcs[1],
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpMkVecV2I8 {
    #[dst_type(V2I8)]
    pub dst: Dst,

    #[src_type(I8)]
    pub srcs: [Src; 2],
}

impl fmt::Display for OpMkVecV2I8 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = MKVEC.v4i8 {} {}",
            &self.dst, &self.srcs[0], &self.srcs[1],
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpMkVecV4I8 {
    #[dst_type(V4I8)]
    pub dst: Dst,

    #[src_type(I8)]
    pub srcs: [Src; 4],
}

impl fmt::Display for OpMkVecV4I8 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = MKVEC.v4i8 {} {} {} {}",
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            &self.srcs[2],
            &self.srcs[3],
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I16, I32])]
pub struct OpMov {
    pub dst: Dst,
    pub dst_type: DataType,
    pub src: Src,
}

impl fmt::Display for OpMov {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} = MOV.{} {}", &self.dst, self.dst_type, &self.src,)
    }
}

#[derive(Clone, FromVariants, Opcode)]
pub enum Op {
    Branch(OpBranch),
    End(OpEnd),
    FAdd(OpFAdd),
    FCmp(OpFCmp),
    MkVecV2I8(OpMkVecV2I8),
    MkVecV4I8(OpMkVecV4I8),
    Mov(OpMov),
}
