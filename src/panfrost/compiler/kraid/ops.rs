// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use kraid_proc::{variants, FromVariants, Opcode};
use std::fmt;

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
    Mov(OpMov),
}
