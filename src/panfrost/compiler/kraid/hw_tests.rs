use std::sync::OnceLock;
use std::{io, slice};

use crate::builder::*;
use crate::data_type::NumericType;
use crate::ir::*;
use crate::model::{Model, model_for_gpu_id};
use crate::ops::*;
use crate::ssa_value::SSAValueAllocator;
use kraid_hw_runner::{HwError, InvocationInfo, TestRunner};

/// Enables libpanfrost_decode logs for debugging purposes.
const DEVICE_DEBUG: bool = false;

/// Even when the test does not use the FAU directly, it is still needed
/// to load CB0 args.
const FAU_ONLY_ARGS: &'static [u32] = &[0u32; 4];

struct RunSingleton {
    model: Box<dyn Model + Sync + Send>,
    runner: TestRunner,
}

static RUN_SINGLETON: OnceLock<RunSingleton> = OnceLock::new();

impl RunSingleton {
    fn create() -> Result<RunSingleton, HwError> {
        let runner = TestRunner::new(DEVICE_DEBUG)?;

        let gpu_id = runner.gpu_id();
        let model = model_for_gpu_id(gpu_id)
            .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;

        Ok(RunSingleton { model, runner })
    }

    pub fn get() -> &'static RunSingleton {
        RUN_SINGLETON.get_or_init(|| {
            RunSingleton::create().expect("Failed to create test device")
        })
    }

    fn execute_raw(&self, info: InvocationInfo) -> Result<(), HwError> {
        assert!(info.invocations <= WARP_SIZE.into());

        self.runner.run(info)
    }

    fn execute(&self, info: InvocationInfo) {
        // Chunk it in various WARP_SIZE runs until we have proper preloaded regs support.
        for chunk_start in (0..info.invocations).step_by(WARP_SIZE as usize) {
            let chunk_end =
                (chunk_start + WARP_SIZE as u16).min(info.invocations);

            let orig_data: &mut [u8] = info.data;
            let data_start: usize =
                info.data_stride as usize * chunk_start as usize;
            let chunk_info = InvocationInfo {
                data: &mut orig_data[data_start..],
                invocations: chunk_end - chunk_start,
                ..info
            };

            self.execute_raw(chunk_info)
                .expect("Error on job submission");
        }
    }
}

fn transmute_slice_to_u8<T: Sized>(data: &[T]) -> &[u8] {
    // SAFETY: we are just transmuting a [u32] to [u8] of same byte-length
    unsafe {
        slice::from_raw_parts(
            data.as_ptr() as *mut u8,
            data.len() * size_of::<T>(),
        )
    }
}

fn transmute_mut_slice_to_u8<T: Sized>(data: &mut [T]) -> &mut [u8] {
    // SAFETY: we are just transmuting a [u32] to [u8] of same byte-length.
    // We are also creating a second mutable slice to the same memory, but that
    // one is connected to the lifetime of the original slice by the function
    // signature. While the u8 slice is live, nobody can use the u32 slice.
    unsafe {
        slice::from_raw_parts_mut(
            data.as_mut_ptr() as *mut u8,
            data.len() * size_of::<T>(),
        )
    }
}

pub struct TestShaderBuilder<'a> {
    model: &'a dyn Model,
    b: InstrBuilder<'a>,
    ssa_alloc: SSAValueAllocator,
    start_block: BasicBlock,
    label: Label,
    data_addr: SrcRef,
    max_data_offset: u16,
}

const WARP_SIZE: u16 = 16;

impl<'a> TestShaderBuilder<'a> {
    pub fn new(model: &'a dyn Model) -> Self {
        let mut label_alloc = LabelAllocator::default();
        let mut ssa_alloc = SSAValueAllocator::default();
        let mut b = SSAInstrBuilder::new(model, &mut ssa_alloc);

        // ABI: struct hw_runner_shader_args
        let data_base_lo = FAURef::user_i32(0);
        let data_base_hi = FAURef::user_i32(1);
        let data_stride = FAURef::user_i32(2);

        let lane_id = FAURef {
            page: FAUPage::Special3,
            idx: 2, // lane_id
            load64: false,
        };

        // TODO: we should actually use preloaded registers here (local_id_1)
        // for now just use lane_id and run only <=16 shaders
        let invoc_id = b.copy_i32(lane_id.into());

        let data_offset = b.alloc_ssa(32);
        b.push_op(OpIMul {
            dst: data_offset.into(),
            dst_type: DataType::U32,
            saturate: false,
            srcs: [data_stride.into(), invoc_id.into()],
        });

        // Just add the lower 32-bits, copy the higher bits and
        // hope we don't test 4GiB of data.
        let data_addr = b.alloc_vec(2);
        b.copy_i32_to(data_addr[1].into(), data_base_hi.into());
        b.push_op(OpIAdd {
            dst: data_addr[0].into(),
            dst_type: DataType::U32,
            saturate: false,
            srcs: [data_base_lo.into(), data_offset.into()],
        });

        let start_block = BasicBlock {
            label: label_alloc.alloc(),
            instrs: b.into_vec(),
        };

        TestShaderBuilder {
            model,
            b: InstrBuilder::new(model),
            ssa_alloc,
            start_block,
            label: label_alloc.alloc(),
            data_addr: data_addr.into(),
            max_data_offset: 0,
        }
    }

    pub fn ld_test_data(&mut self, offset: u16, bits: u8) -> SSARef {
        let comps = bits.div_ceil(32);
        let dst: SSARef = if comps == 1 {
            self.ssa_alloc.alloc(bits).into()
        } else {
            self.ssa_alloc.alloc_vec(comps)
        };

        self.max_data_offset = self.max_data_offset.max(offset);

        self.push_op(OpLoad {
            dst: dst.clone().into(),
            dst_type: DataType::get(1, NumericType::Integer, bits),
            access: MemAccess::None,
            addr: self.data_addr.clone().into(),
            offset: offset.try_into().unwrap(),
        });

        dst
    }

    pub fn st_test_data(&mut self, offset: u16, data: SSARef) {
        self.max_data_offset = self.max_data_offset.max(offset);

        self.push_op(OpStore {
            src_type: DataType::get(1, NumericType::Integer, data.bytes() * 8),
            access: MemAccess::None,
            data: data.into(),
            addr: self.data_addr.clone().into(),
            offset: offset.try_into().unwrap(),
        });
    }

    fn compile(self) -> CompiledTestCase {
        let Self {
            model,
            mut b,
            ssa_alloc,
            start_block,
            label,
            max_data_offset,
            ..
        } = self;

        let exit = b.push_op(OpNop {});
        exit.flow.set_end_shader();

        let test_block = BasicBlock {
            label,
            instrs: b.into_vec(),
        };

        let mut s = Shader {
            model,
            ssa_alloc,
            blocks: vec![start_block, test_block],
        };
        //eprintln!("\nRIGHT AFTER CONSTR: {}", &s);
        s.validate();

        s.widen_alu_ops();
        s.validate();

        s.legalize_src_swizzles();
        s.validate();

        s.lower_mkvec_swz();
        s.validate();

        s.lower_small_constants();
        s.validate();

        s.assign_registers();
        s.validate();

        s.lower_copy();
        s.validate();

        s.assign_message_slots();
        s.validate();

        //eprintln!("\nBEFORE ENCODE: {}", &s);
        let bin = model.encode_shader(&s);

        CompiledTestCase {
            code: bin,
            max_data_offset,
            // ABI: we always load the CB0 args at offset 0 for now
            fau_args_offset: 0,
            // ??
            register_count: 32,
        }
    }
}

impl Builder for TestShaderBuilder<'_> {
    fn arch(&self) -> u8 {
        self.b.arch()
    }

    fn model(&self) -> &dyn Model {
        self.b.model()
    }

    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.b.push_instr(instr)
    }
}

impl SSABuilder for TestShaderBuilder<'_> {
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue {
        self.ssa_alloc.alloc(bits)
    }

    fn alloc_vec(&mut self, comps: u8) -> SSARef {
        self.ssa_alloc.alloc_vec(comps)
    }
}

struct CompiledTestCase {
    code: Vec<u32>,
    register_count: u16,
    max_data_offset: u16,
    fau_args_offset: usize,
}

impl CompiledTestCase {
    fn with_args_raw<'a>(
        &'a self,
        fau: &'a [u32],
        data: &'a mut [u8],
        data_stride: u32,
        invocations: u16,
    ) -> InvocationInfo<'a> {
        // We need preloaded registers support to distinguish between invocations
        InvocationInfo {
            code: transmute_slice_to_u8(&self.code),
            fau,
            fau_args_offset: self.fau_args_offset,
            data,
            data_stride,
            register_count: self.register_count,
            invocations,
        }
    }

    fn with_args<'a, T>(
        &'a self,
        fau: &'a [u32],
        data: &'a mut [T],
    ) -> InvocationInfo<'a> {
        let invocations = data.len().try_into().expect("Too many invocations");
        let data_stride = size_of::<T>().try_into().unwrap();
        let data_raw = transmute_mut_slice_to_u8(data);
        self.with_args_raw(fau, data_raw, data_stride, invocations)
    }
}

#[test]
fn test_sanity() {
    let run = RunSingleton::get();
    let b = TestShaderBuilder::new(&*run.model);
    let bin = b.compile();
    let mut data = [0u32; WARP_SIZE as usize];
    let case = bin.with_args(FAU_ONLY_ARGS, &mut data);
    run.execute(case);
}

#[test]
fn test_copy_single() {
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(&*run.model);
    let data = b.ld_test_data(0, 32);
    b.st_test_data(4, data.into());

    let bin = b.compile();
    // First, do a small copy (32-bits)
    let mut data = [42, 67, 31, 41, 0, 0, 0, 0];
    let case = bin.with_args_raw(FAU_ONLY_ARGS, &mut data, 0, WARP_SIZE);

    run.execute(case);
    assert_eq!(&data[0..4], &data[4..8]);
}

#[test]
fn test_copy_warp() {
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(&*run.model);
    let data = b.ld_test_data(0, 32);
    b.st_test_data(4 * WARP_SIZE, data.into());

    let bin = b.compile();

    const READ_SIZE: usize = 4 * WARP_SIZE as usize;
    let mut data = [0u8; READ_SIZE * 2];
    for i in 0..READ_SIZE {
        data[i] = (i as u8) * 4 + 1;
    }
    let case = bin.with_args_raw(FAU_ONLY_ARGS, &mut data, 4, WARP_SIZE);

    run.execute(case);
    assert_eq!(&data[..READ_SIZE], &data[READ_SIZE..]);

    // Now do the same with half the invocations and check that it
    // only copies the first half
    const HALF_READ_SIZE: usize = READ_SIZE / 2;
    for i in 0..data.len() {
        data[i] = (i as u8) * 4 + 1;
    }
    data[READ_SIZE..].fill(0);
    let case = bin.with_args_raw(FAU_ONLY_ARGS, &mut data, 4, WARP_SIZE / 2);

    run.execute(case);
    assert_eq!(
        &data[..HALF_READ_SIZE],
        &data[READ_SIZE..(READ_SIZE + HALF_READ_SIZE)]
    );
    assert_eq!(&data[(READ_SIZE + HALF_READ_SIZE)..], &[0; HALF_READ_SIZE])
}

#[test]
fn test_copy_large() {
    // Test with more than warp_size invocations
    // In the future this will be fully supported, but we don't yet have preloaded regs
    // Can we really emulate it?
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(&*run.model);
    let data = b.ld_test_data(0, 32);
    b.st_test_data(4 * 2 * WARP_SIZE, data.into());

    let bin = b.compile();

    const READ_SIZE: usize = 4 * (2 * WARP_SIZE) as usize;
    let mut data = [0u8; READ_SIZE * 2];
    for i in 0..READ_SIZE {
        data[i] = (i as u8) * 4 + 1;
    }
    let case = bin.with_args_raw(FAU_ONLY_ARGS, &mut data, 4, 2 * WARP_SIZE);

    run.execute(case);
    assert_eq!(&data[..READ_SIZE], &data[READ_SIZE..]);
}
