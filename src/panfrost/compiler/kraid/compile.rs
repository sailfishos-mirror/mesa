// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::model::model_for_gpu_id;
use compiler::bindings::*;
use kraid_bindings::*;
use std::cmp::max;

fn dump_shader(s: &Shader, suffix: &str) {
    let s = format!("{s}");

    let mut max_eq_pos = 0_usize;
    for line in s.lines() {
        if let Some(pos) = line.find("=") {
            max_eq_pos = max(max_eq_pos, pos);
        }
    }

    let mut out = String::new();
    for line in s.lines() {
        out.push_str("\n");

        let line = line.trim_end();
        if line.is_empty() {
            continue;
        }

        if line.starts_with("__") {
            out.push_str(line);
        } else if let Some(pos) = line.find("=") {
            debug_assert!(pos <= max_eq_pos);
            for _ in 0..(max_eq_pos - pos) {
                out.push_str(" ");
            }
            out.push_str(line);
        } else {
            let line = line.trim_start();
            for _ in 0..(max_eq_pos + 2) {
                out.push_str(" ");
            }
            out.push_str(line);
        }
    }

    eprintln!("Kraid shader {suffix}:{out}");
}

#[no_mangle]
pub extern "C" fn kraid_compile_nir(
    nir: &mut nir_shader,
    inputs: &pan_compile_inputs,
    _binary: &mut util_dynarray,
    _info: &mut pan_shader_info,
) {
    let model = model_for_gpu_id(inputs.gpu_id).unwrap();

    eprint!("{}", nir.to_string().unwrap());

    let mut s = Shader::from_nir(model.as_ref(), nir);
    dump_shader(&s, "after translation from NIR");
    s.validate();

    s.assign_registers();
    dump_shader(&s, "after register assignment");
    s.validate();

    todo!("Compile to binaries");
}
