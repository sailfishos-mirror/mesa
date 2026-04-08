// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use compiler::bindings::*;
use kraid_bindings::*;

#[no_mangle]
pub extern "C" fn kraid_compile_nir(
    _nir: &mut nir_shader,
    _inputs: &pan_compile_inputs,
    _binary: &mut util_dynarray,
    _info: &mut pan_shader_info,
) {
    todo!("Implement a compiler");
}
