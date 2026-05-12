// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

#![allow(non_upper_case_globals)]

use crate::builder::*;
use crate::ir::*;
use crate::ops::*;
use crate::ssa_value::SSAValueAllocator;
use compiler::bindings::*;
use compiler::nir::*;
use rustc_hash::FxHashMap;
use std::cmp::max;

#[derive(Default)]
struct BlockLabelMap {
    map: FxHashMap<u32, Label>,
}

impl BlockLabelMap {
    fn add(&mut self, block: &nir_block, label: Label) {
        self.map
            .entry(block.index)
            .and_modify(|_| panic!("Cannot set an block label twice"))
            .or_insert(label);
    }

    fn get(&self, block: &nir_block) -> Label {
        *self.map.get(&block.index).expect("Unknown block")
    }
}

struct ShaderFromNir<'a> {
    model: &'a dyn Model,
    nir: &'a nir_shader,
    ssa_map: FxHashMap<u32, Vec<SSAValue>>,
}

impl<'a> ShaderFromNir<'a> {
    fn new(model: &'a dyn Model, nir: &'a nir_shader) -> Self {
        ShaderFromNir {
            model,
            nir,
            ssa_map: Default::default(),
        }
    }

    fn alloc_ssa(&mut self, b: &mut impl SSABuilder, def: &nir_def) -> SSARef {
        // 8-bit isn't real and can't hurt you
        let bits = max(def.bit_size * def.num_components, 16);
        let mut vec = Vec::new();
        if bits == 16 {
            vec.push(b.alloc_ssa(16));
        } else {
            for _ in 0..bits.div_ceil(32) {
                vec.push(b.alloc_ssa(32));
            }
        }
        let ssa = SSARef::try_from(vec.as_slice()).unwrap();
        self.set_ssa(def, vec);
        ssa
    }

    fn set_ssa(&mut self, def: &nir_def, vec: Vec<SSAValue>) {
        self.ssa_map
            .entry(def.index)
            .and_modify(|_| panic!("Cannot set an SSA def twice"))
            .or_insert(vec);
    }

    fn get_ssa(&self, ssa: &nir_def) -> &[SSAValue] {
        self.ssa_map.get(&ssa.index).unwrap()
    }

    fn get_src_ssa(&self, src: &nir_src) -> SSARef {
        self.get_ssa(src.as_def())
            .try_into()
            .expect("Source too large")
    }

    fn get_src(&self, src: &nir_src) -> Src {
        self.get_src_ssa(src).into()
    }

    fn parse_const(
        &mut self,
        b: &mut impl SSABuilder,
        load: &nir_load_const_instr,
    ) {
        let mut imm_u32 = Vec::new();
        match load.def.bit_size {
            8 => {
                // SAFETY: def.bit_size == 8
                let mut i = load.values().iter().map(|v| unsafe { v.u8_ });
                for _ in 0..load.def.num_components.div_ceil(4) {
                    let v = [
                        i.next().unwrap_or(0),
                        i.next().unwrap_or(0),
                        i.next().unwrap_or(0),
                        i.next().unwrap_or(0),
                    ];
                    imm_u32.push(u32::from_le_bytes(v))
                }
            }
            16 => {
                // SAFETY: def.bit_size == 32
                let mut i = load.values().iter().map(|v| unsafe { v.u16_ });
                for _ in 0..load.def.num_components.div_ceil(2) {
                    let x = i.next().unwrap_or(0);
                    let y = i.next().unwrap_or(0);
                    imm_u32.push((x as u32) | ((y as u32) << 16));
                }
            }
            32 => {
                // SAFETY: def.bit_size == 32
                let i = load.values().iter().map(|v| unsafe { v.u32_ });
                imm_u32 = i.collect();
            }
            64 => {
                // SAFETY: def.bit_size == 64
                for v in load.values().iter().map(|v| unsafe { v.u64_ }) {
                    imm_u32.push(v as u32);
                    imm_u32.push((v >> 32) as u32);
                }
            }
            bit_size => panic!("Unsupported bit size: {bit_size}"),
        }

        let bits = load.def.bit_size * load.def.num_components;
        assert_eq!(imm_u32.len(), usize::from(bits.div_ceil(32)));

        if bits <= 16 {
            let ssa = b.mov_i16(Src::imm_u16(imm_u32[0] as u16));
            self.set_ssa(&load.def, vec![ssa]);
        } else {
            self.set_ssa(
                &load.def,
                imm_u32.into_iter().map(|u| b.mov_i32(u.into())).collect(),
            );
        }
    }

    fn parse_block(
        &mut self,
        ssa_alloc: &mut SSAValueAllocator,
        block_map: &BlockLabelMap,
        nb: &nir_block,
    ) -> BasicBlock {
        let mut b = SSAInstrBuilder::new(self.model.arch(), ssa_alloc);

        for ni in nb.iter_instr_list() {
            match ni.type_ {
                nir_instr_type_load_const => {
                    self.parse_const(&mut b, ni.as_load_const().unwrap())
                }
                _ => panic!("Unsupported instruction type"),
            }
        }

        let succ = nb.successors();
        if nb.cf_tree_next().is_none() {
            b.push_op(OpEnd {});
        } else if let Some(nif) = nb.following_if() {
            let succ = [succ[0].unwrap(), succ[1].unwrap()];

            b.push_op(OpBranch {
                not: true,
                cond: self.get_src(&nif.condition),
                combine_op: BranchCombineOp::None,
                label: block_map.get(succ[1]),
            });
        } else {
            assert!(succ[1].is_none());
            if let Some(succ) = succ[0] {
                b.push_op(OpBranch {
                    not: true,
                    cond: 0.into(),
                    combine_op: BranchCombineOp::None,
                    label: block_map.get(succ),
                });
            }
        }

        BasicBlock {
            label: block_map.get(nb),
            instrs: b.into_vec(),
        }
    }

    fn parse_shader(mut self) -> Shader<'a> {
        let nfi = self.nir.get_entrypoint().unwrap();
        let mut ssa_alloc = Default::default();

        // Pre-populate the block table so we have the same numbering as NIR
        let mut label_alloc: LabelAllocator = Default::default();
        let mut block_map: BlockLabelMap = Default::default();
        for nb in nfi.iter_blocks() {
            block_map.add(nb, label_alloc.alloc());
        }

        let blocks = nfi
            .iter_blocks()
            .map(|nb| self.parse_block(&mut ssa_alloc, &block_map, nb))
            .collect();

        Shader {
            model: self.model,
            ssa_alloc,
            blocks,
        }
    }
}

impl<'a> Shader<'a> {
    pub fn from_nir(model: &'a dyn Model, nir: &'a nir_shader) -> Self {
        ShaderFromNir::new(model, nir).parse_shader()
    }
}
