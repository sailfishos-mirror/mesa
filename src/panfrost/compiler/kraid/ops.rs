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
    MkVecV2I8(OpMkVecV2I8),
    MkVecV4I8(OpMkVecV4I8),
    Mov(OpMov),
}
