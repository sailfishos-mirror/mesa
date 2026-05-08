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
struct PhiAllocMap {}

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

    fn parse_shader(self) -> Shader<'a> {
        let _nfi = self.nir.get_entrypoint().unwrap();
        let ssa_alloc = Default::default();

        Shader {
            model: self.model,
            ssa_alloc,
            blocks: Default::default(),
        }
    }
}

impl<'a> Shader<'a> {
    pub fn from_nir(model: &'a dyn Model, nir: &'a nir_shader) -> Self {
        ShaderFromNir::new(model, nir).parse_shader()
    }
}
