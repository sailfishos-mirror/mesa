// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::encode_v9::*;
use crate::ir::*;
use kraid_bindings::*;

pub trait Model {
    fn arch(&self) -> u8;

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32>;
}

struct ValhallModel {
    arch: u8,
}

impl Model for ValhallModel {
    fn arch(&self) -> u8 {
        self.arch
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        encode_v9(s, self.arch)
    }
}

pub fn model_for_gpu_id(gpu_id: u64) -> Result<Box<dyn Model>, &'static str> {
    // SAFETY: pan_arch() just translates one integer to another
    let arch = u8::try_from(unsafe { pan_arch(gpu_id) }).unwrap();

    if arch >= 15 {
        Err("Kraid does not yet support this GPU")
    } else if arch >= 9 {
        Ok(Box::new(ValhallModel { arch }))
    } else {
        Err("Kraid only supports Valhall (v9) and later GPUs")
    }
}
