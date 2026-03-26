use std::{collections::HashMap, ffi::CString, sync::Arc};

use mesa_rust_util::properties::Properties;
use rusticl_kernels::RUSTICL_BUILTIN_KERNELS_SPV;

use crate::core::{
    context::Context,
    device::Device,
    kernel::Kernel,
    program::{CompileOptions, Program},
};

pub struct Meta {
    kernels: HashMap<CString, Arc<Kernel>>,
}

impl Meta {
    pub fn new(devs: &'static [Device]) -> Self {
        let spirv = RUSTICL_BUILTIN_KERNELS_SPV;
        let devs = devs.iter().collect::<Vec<_>>();
        let ctx = Context::new(devs.clone(), Properties::default(), None);
        let prog = Program::from_spirv(ctx, spirv);
        let options = CompileOptions::new(c"", -1).unwrap();
        prog.build(devs, options, None).unwrap();
        let build = prog.build_info();

        let mut kernels = HashMap::new();
        for kernel in build.kernels() {
            kernels.insert(
                kernel.to_owned(),
                Kernel::new(kernel.to_owned(), Arc::clone(&prog), &build),
            );
        }

        Meta { kernels: kernels }
    }
}
