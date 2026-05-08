// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::ops::*;
use crate::ssa_value::SSAValueAllocator;

pub trait Builder {
    fn arch(&self) -> u8;

    fn push_instr(&mut self, instr: Instr) -> &mut Instr;

    fn push_op(&mut self, op: impl Into<Op>) -> &mut Instr {
        self.push_instr(Instr::from(op))
    }
}

pub trait SSABuilder: Builder {
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue;

    fn mov_i16(&mut self, src: Src) -> SSAValue {
        let def = self.alloc_ssa(16);
        self.push_op(OpMov {
            dst: def.into(),
            dst_type: DataType::I16,
            src,
        });
        def
    }

    fn mov_i32(&mut self, src: Src) -> SSAValue {
        let def = self.alloc_ssa(32);
        self.push_op(OpMov {
            dst: def.into(),
            dst_type: DataType::I32,
            src,
        });
        def
    }
}

pub struct InstrBuilder {
    arch: u8,
    instrs: Vec<Instr>,
}

impl InstrBuilder {
    pub fn new(arch: u8) -> Self {
        InstrBuilder {
            arch,
            instrs: Default::default(),
        }
    }

    pub fn into_vec(self) -> Vec<Instr> {
        self.instrs
    }
}

impl Builder for InstrBuilder {
    fn arch(&self) -> u8 {
        self.arch
    }

    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.instrs.push(instr);
        self.instrs.last_mut().unwrap()
    }
}

pub struct SSAInstrBuilder<'a> {
    b: InstrBuilder,
    alloc: &'a mut SSAValueAllocator,
}

impl<'a> SSAInstrBuilder<'a> {
    pub fn new(
        arch: u8,
        alloc: &'a mut SSAValueAllocator,
    ) -> SSAInstrBuilder<'a> {
        SSAInstrBuilder {
            b: InstrBuilder::new(arch),
            alloc,
        }
    }

    pub fn into_vec(self) -> Vec<Instr> {
        self.b.into_vec()
    }
}

impl Builder for SSAInstrBuilder<'_> {
    fn arch(&self) -> u8 {
        self.b.arch()
    }

    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.b.push_instr(instr)
    }
}

impl SSABuilder for SSAInstrBuilder<'_> {
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue {
        self.alloc.alloc(bits)
    }
}
