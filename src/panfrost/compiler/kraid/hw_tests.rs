use core::fmt;
use std::f32::consts::{E, PI};
use std::ops::Range;
use std::sync::OnceLock;
use std::{io, iter, slice};

use crate::builder::*;
use crate::data_type::NumericType;
use crate::foldable::{FoldData, Foldable};
use crate::ir::*;
use crate::model::{Model, model_for_gpu_id};
use crate::ops::*;
use crate::ssa_value::{AllocSSA, SSAValueAllocator};
use crate::swizzle::AsmSwizzleWiden;
use acorn::Acorn;
use compiler::cfg::CFGBuilder;
use kraid_hw_runner::{HwError, InvocationInfo, TestRunner};
use rustc_hash::FxBuildHasher;

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

    fn try_execute(&self, info: InvocationInfo) -> Result<(), HwError> {
        self.runner.run(info)
    }

    fn execute(&self, info: InvocationInfo) {
        self.try_execute(info).expect("Error on job submission");
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
    info: ShaderInfo,
    ssa_alloc: SSAValueAllocator,
    start_block: BasicBlock,
    data_addr: SrcRef,
    max_data_offset: u16,
}

const WARP_SIZE: u32 = 16;

impl<'a> TestShaderBuilder<'a> {
    pub fn new(model: &'a dyn Model) -> Self {
        let mut label_alloc = LabelAllocator::default();
        let mut ssa_alloc = SSAValueAllocator::default();
        let mut b = SSAInstrBuilder::new(model, &mut ssa_alloc);
        let mut info = ShaderInfo::default();

        // ABI: struct hw_runner_shader_args
        let data_base_lo = FAURef::user_i32(0);
        let data_base_hi = FAURef::user_i32(1);
        let data_stride = FAURef::user_i32(2);

        let invoc_id: SSAValue = b.alloc_ssa(32);
        let global_id_reg = model.preload_reg(PreloadReg::GlobalId0).unwrap();
        info.register_preload |= 1 << global_id_reg.idx;
        b.push_op(OpRegIn {
            dst: invoc_id.into(),
            dst_type: DataType::I32,
            reg: global_id_reg,
        });

        let data_offset = b.alloc_ssa(32);
        b.push_op(OpIMul {
            dst: data_offset.into(),
            dst_type: DataType::U32,
            saturate: false,
            srcs: [data_stride.into(), invoc_id.into()],
        });

        // Just add the lower 32-bits, copy the higher bits and
        // hope we don't test 4GiB of data.
        let data_addr = b.alloc_ref(64);
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
            info,
            ssa_alloc,
            start_block,
            data_addr: data_addr.into(),
            max_data_offset: 0,
        }
    }

    pub fn ld_test_data(&mut self, offset: u16, bits: u8) -> SSARef {
        let dst = self.alloc_ref(bits.into());

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
            info,
            ssa_alloc,
            mut start_block,
            max_data_offset,
            ..
        } = self;

        let exit = b.push_op(OpNop {});
        exit.flow.set_end_shader();

        start_block.instrs.extend(b.into_mapped());
        let mut cfg: CFGBuilder<Label, BasicBlock, FxBuildHasher> =
            CFGBuilder::new();
        cfg.add_node(start_block.label, start_block);

        let mut s = Shader {
            model,
            ssa_alloc,
            phi_alloc: Default::default(),
            blocks: cfg.as_cfg(false),
            info,
        };
        s.validate();

        pass!(s.remat_constants());
        pass!(s.widen_alu_ops());
        pass!(s.legalize_src_swizzles());
        pass!(s.opt_copy_prop());
        pass!(s.lower_mkvec_swz());
        pass!(s.opt_dce());
        pass!(s.lower_small_constants());
        pass!(s.legalize());
        pass!(s.assign_registers());
        pass!(s.lower_copy());
        pass!(s.assign_message_slots());

        let bin = model.encode_shader(&s);

        CompiledTestCase {
            code: bin,
            max_data_offset,
            // ABI: we always load the CB0 args at offset 0 for now
            fau_args_offset: 0,
            info: s.info,
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

impl AllocSSA for TestShaderBuilder<'_> {
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue {
        self.ssa_alloc.alloc_ssa(bits)
    }
}

struct CompiledTestCase {
    code: Vec<u32>,
    info: ShaderInfo,
    max_data_offset: u16,
    fau_args_offset: usize,
}

impl CompiledTestCase {
    fn with_args_raw<'a>(
        &'a self,
        fau: &'a [u32],
        data: &'a mut [u8],
        data_stride: u32,
        invocations: u32,
    ) -> InvocationInfo<'a> {
        // We need preloaded registers support to distinguish between invocations
        InvocationInfo {
            code: transmute_slice_to_u8(&self.code),
            fau,
            fau_args_offset: self.fau_args_offset,
            data,
            data_stride,
            register_preload: self.info.register_preload,
            register_count: self.info.registers_used,
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
    b.st_test_data(4 * WARP_SIZE as u16, data.into());

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
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(&*run.model);
    let data = b.ld_test_data(0, 32);
    b.st_test_data(4 * 2 * WARP_SIZE as u16, data.into());

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

#[test]
fn test_fadd_lscale() {
    // The hardware docs tell that src1 is scaled to the range [0.5, 1.75)
    // how is this scaled exactly?
    // TODO: convert to Foldable test when we add proper float Foldables
    let run = RunSingleton::get();
    let shader = {
        let mut b = TestShaderBuilder::new(&*run.model);
        let data = b.ld_test_data(0, 32);

        let res = b.alloc_ssa(32);
        b.push_op(OpFAddLScale {
            dst: res.into(),
            round: FRound::NearestEven,
            clamp: FClamp::None,
            srcs: [Src::from(0).fneg(), data.into()],
        });

        b.st_test_data(4, res.into());
        b.compile()
    };

    let case_fn = |c: f32| [c.to_bits(), 0];
    let mut data = vec![
        case_fn(1.0),
        case_fn(-1.0),
        case_fn(1.5),
        case_fn(0.75),
        case_fn(3.0),
        case_fn(-3.0),
        case_fn(0.0),
        case_fn(-0.0),
        case_fn(10.0),
        case_fn(-10.0),
        case_fn(-10.0),
        case_fn(PI),
        case_fn(-PI),
        case_fn(f32::from_bits(1)), // Denormals
        case_fn(f32::INFINITY),
        case_fn(-f32::INFINITY),
    ];

    let case = shader.with_args(FAU_ONLY_ARGS, &mut data);
    run.execute(case);

    fn model(x: f32) -> f32 {
        if !x.is_normal() {
            return x;
        }
        // Force exponent to 0
        let m = f32::from_bits((x.to_bits() & 0x807fffff) | 0x3f800000);
        // Mantissa is (by construction) in [1.0, 2.0), half it if too large
        if m.abs() >= 1.5 { m * 0.5 } else { m }
    }

    for data in &data {
        let [input, res] = data.map(f32::from_bits);
        assert_eq!(res, model(input), "Failed for {input}");
    }
}

fn parse_folded(folded: &mut [u64], words: &[u32], types: DataTypeIter) {
    let mut offset = 0;
    for (comp, dtype) in folded.iter_mut().zip(types) {
        match dtype.total_bits() {
            8 | 16 | 32 => {
                *comp = words[offset] as u64;
                offset += 1;
            }
            64 => {
                *comp = words[offset] as u64 | (words[offset + 1] as u64) << 32;
                offset += 2;
            }
            _ => panic!("Invalid data size"),
        }
    }
    assert_eq!(offset, words.len());
}

pub fn test_foldable_op_with(
    mut op: impl Foldable + Clone + Into<Op> + fmt::Display,
    mut rand_u32: impl FnMut(usize) -> u32,
) {
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(&*run.model);

    let mut offset_words = 0u16;
    let mut word_bits = Vec::new();

    for (src, src_type) in op.srcs_types_mut() {
        let read_bits = src_type.total_bits();
        let words = read_bits.div_ceil(32);
        let data = b.ld_test_data(offset_words * 4, words * 32);
        offset_words += u16::from(words);

        let swiz_src = if src.swizzle.is_none() {
            // We always load words.  If there's no swizzle, we need to at
            // least trim it down to fit inside the source type.
            let src_type_swizzle = match read_bits {
                8 => Swizzle::replicate_byte(0),
                16 => Swizzle::replicate_half(0),
                _ => Swizzle::NONE,
            };
            Src::from(data).swizzle(src_type_swizzle)
        } else {
            Src::from(data).swizzle(src.swizzle)
        };
        src.src_ref = swiz_src.src_ref;
        src.swizzle = swiz_src.swizzle;

        word_bits.extend(iter::repeat_n(read_bits.min(32), words as usize));
    }
    let src_words = usize::from(offset_words);

    let mut fold_src = vec![0u64; op.srcs().len() as usize];
    let mut fold_dst = vec![0u64; op.dsts().len() as usize];
    for (dst, dst_type) in op.dsts_types_mut() {
        let write_bits = dst_type.total_bits();
        dst.dst_ref = b.alloc_ref(write_bits.into()).into();
        dst.lanes = match (dst.lanes, write_bits) {
            (DstLanes::None | DstLanes::All, 8) => DstLanes::B0,
            (DstLanes::None | DstLanes::All, 16) => DstLanes::H0,
            (DstLanes::None | DstLanes::All, _) => DstLanes::All,
            (DstLanes::AnyB, _) => DstLanes::B0,
            (DstLanes::AnyH, _) => DstLanes::H0,
            (lanes, _) => lanes,
        };
    }

    b.push_op(op.clone());
    let op = op; // Remove mutability

    for dst in op.dsts() {
        let DstRef::SSA(ssa) = &dst.dst_ref else {
            unreachable!(); // We set it as SSA before
        };
        b.st_test_data(offset_words * 4, ssa.clone());
        assert!(ssa.bytes() % 4 == 0);
        offset_words += u16::from(ssa.bytes() / 4);
    }
    let total_words = usize::from(offset_words);
    let dst_words = total_words - src_words;

    let bin = b.compile();

    // We're throwing random data at it here so the idea is that the number
    // of test cases we need to get good coverage is relative to the square
    // of the number of components.  For a big op with 3 srcs, this is going
    // to give us 2500 iterations. (copied from NAK)
    let invocations = src_words * src_words * 100;
    let mut data = Vec::with_capacity(invocations * (src_words + dst_words));

    assert!(src_words == word_bits.len());
    for _ in 0..invocations {
        data.extend(word_bits.iter().enumerate().map(|(i, bits)| {
            rand_u32(i) & (u32::MAX >> (u32::BITS - *bits as u32))
        }));
        data.extend(iter::repeat_n(0, dst_words));
    }
    assert!(data.len() == invocations * (src_words + dst_words));

    let data_bytes = transmute_mut_slice_to_u8(&mut data);
    let case = bin.with_args_raw(
        FAU_ONLY_ARGS,
        data_bytes,
        4 * total_words as u32,
        invocations.try_into().unwrap(),
    );

    run.execute(case);

    let mut hw_dst = fold_dst.clone();
    for invoc_id in 0..invocations {
        let data_off = invoc_id * total_words;
        let data = &data[data_off..(data_off + total_words)];

        parse_folded(&mut fold_src, &data[..src_words], op.src_types());
        parse_folded(&mut hw_dst, &data[src_words..], op.dst_types());

        fold_dst.fill(0);
        let mut fold = FoldData {
            dsts: &mut fold_dst,
            srcs: &fold_src,
            op: &op,
        };
        op.fold(&*run.model, &mut fold);

        if hw_dst != fold_dst {
            eprintln!("Foldable test data mismatch for {op}:");
            eprintln!("| Input:    {:?}", &fold_src);
            eprintln!("| Hardware: {:?}", &hw_dst);
            eprintln!("| Folded:   {:?}", &fold_dst);
            panic!("Folding test data mismatch");
        }
    }
}

pub fn test_foldable_op(op: impl Foldable + Clone + Into<Op> + fmt::Display) {
    let mut a = Acorn::new();
    test_foldable_op_with(op, |_| a.get_u32());
}

#[test]
fn test_op_bitrev() {
    let op = OpBitRev {
        dst: DstRef::None.into(),
        src: 0.into(),
    };

    test_foldable_op(op);
}

#[test]
fn test_op_clz() {
    const DATA_TYPES: &'static [DataType] =
        &[DataType::U32, DataType::V2U16, DataType::V4U8];

    // The .mask modifier only outputs all-bits if the input value is 0
    // Test some edge-cases then test random data
    let mut edge_cases: Vec<u32> = vec![
        0x0000_0000, // CLZ -> 32, .mask -> 0xffffffff
        0xffff_ffff,
        0x8000_0000,
        0x7fff_ffff,
        0x0000_0002,
        0x0000_0003,
    ];
    for n in 0..32 {
        edge_cases.push(1u32 << n);
        edge_cases.push(u32::MAX >> n);
    }

    for &src_type in DATA_TYPES {
        for mask in [false, true] {
            let op = OpClz {
                dst: DstRef::None.into(),
                src_type,
                mask,
                src: 0.into(),
            };

            let mut a = Acorn::new();
            let mut idx = 0usize;
            test_foldable_op_with(op, |_| {
                let v = edge_cases
                    .get(idx)
                    .copied()
                    .unwrap_or_else(|| a.get_u32() >> (a.get_u32() % 32));
                idx += 1;
                v
            });
        }
    }
}

#[test]
fn test_op_csel() {
    const DATA_TYPES: &'static [DataType] = &[
        DataType::S32,
        DataType::U32,
        DataType::F32,
        DataType::V2S16,
        DataType::V2U16,
        DataType::V2F16,
    ];

    const CMP_OPS: &'static [CmpOp] = &[
        CmpOp::Eq,
        CmpOp::Gt,
        CmpOp::Ge,
        CmpOp::Ne,
        CmpOp::Lt,
        CmpOp::Le,
        CmpOp::GtLt,
        CmpOp::Total,
    ];

    for &cmp_type in DATA_TYPES {
        for &cmp_op in CMP_OPS {
            if cmp_type.num_type() != NumericType::Float
                && matches!(cmp_op, CmpOp::GtLt | CmpOp::Total)
            {
                continue;
            }

            let op = OpCSel {
                dst: DstRef::None.into(),
                cmp_type,
                cmp_op,
                cmp_srcs: [0.into(), 0.into()],
                sel_srcs: [0.into(), 0.into()],
            };
            test_foldable_op(op);
        }
    }
}

#[test]
fn test_op_fcmp() {
    const DATA_TYPES: &'static [DataType] = &[DataType::F32, DataType::V2F16];

    const CMP_OPS: &'static [CmpOp] = &[
        CmpOp::Eq,
        CmpOp::Gt,
        CmpOp::Ge,
        CmpOp::Ne,
        CmpOp::Lt,
        CmpOp::Le,
        CmpOp::GtLt,
        CmpOp::Total,
    ];

    const ACCUM_OPS: &'static [CmpAccumOp] =
        &[CmpAccumOp::None, CmpAccumOp::And, CmpAccumOp::Or];

    const RES_TYPES: &'static [CmpResultType] =
        &[CmpResultType::I1, CmpResultType::F1, CmpResultType::M1];

    let mut a = Acorn::new();
    for &src_type in DATA_TYPES {
        for &cmp_op in CMP_OPS {
            for &accum_op in ACCUM_OPS {
                for &res_type in RES_TYPES {
                    let op = OpFCmp {
                        dst: DstRef::None.into(),
                        src_type,
                        res_type,
                        cmp_op,
                        srcs: [0.into(), 0.into()],
                        accum: 0.into(),
                        accum_op,
                    };
                    // Accum is always treated as a bool so let's use 0-1
                    // (otherwise it would always be true)
                    test_foldable_op_with(op, |i| match i {
                        2 => a.get_u32() % 2,
                        _ => a.get_u32(),
                    });
                }
            }
        }
    }
}

#[test]
fn test_op_iabs() {
    const DATA_TYPES: &'static [DataType] = &[DataType::V2S16, DataType::S32];

    const WIDENS: &'static [AsmSwizzleWiden] = &[
        AsmSwizzleWiden::None,
        AsmSwizzleWiden::H0,
        AsmSwizzleWiden::B0,
        AsmSwizzleWiden::B2,
    ];

    for &dst_type in DATA_TYPES {
        for widen in WIDENS {
            let Some(src0_swizzle) = widen.to_swizzle(dst_type) else {
                continue;
            };

            let op = OpIAbs {
                dst: DstRef::None.into(),
                src: Src::from(0).swizzle(src0_swizzle),
                dst_type,
            };
            test_foldable_op(op);
        }
    }
}

#[test]
fn test_op_iadd() {
    const DATA_TYPES: &'static [DataType] = &[
        DataType::V2S16,
        DataType::V2U16,
        DataType::S32,
        DataType::U32,
        DataType::S64,
        DataType::U64,
    ];

    const WIDENS: &'static [AsmSwizzleWiden] = &[
        AsmSwizzleWiden::None,
        AsmSwizzleWiden::B00,
        AsmSwizzleWiden::B02,
        AsmSwizzleWiden::B20,
        AsmSwizzleWiden::H00,
        AsmSwizzleWiden::H10,
        AsmSwizzleWiden::H0,
        AsmSwizzleWiden::H1,
        // AsmSwizzleWiden::W0, // TODO: 64-bit swizzles
        // AsmSwizzleWiden::W1,
    ];

    for &dst_type in DATA_TYPES {
        for widen in WIDENS {
            let Some(src0_swizzle) = widen.to_swizzle(dst_type) else {
                continue;
            };
            for saturate in [false, true] {
                // Not supported by hw
                if saturate && dst_type.bits() == 64 {
                    continue;
                }

                let op = OpIAdd {
                    dst: DstRef::None.into(),
                    srcs: [Src::from(0).swizzle(src0_swizzle), 0.into()],
                    dst_type,
                    saturate,
                };
                test_foldable_op(op);
            }
        }
    }
}

#[test]
fn test_op_icmp() {
    const DATA_TYPES: &'static [DataType] = &[
        DataType::V2S16,
        DataType::V2U16,
        DataType::S32,
        DataType::U32,
    ];

    const CMP_OPS: &'static [CmpOp] = &[
        CmpOp::Eq,
        CmpOp::Gt,
        CmpOp::Ge,
        CmpOp::Ne,
        CmpOp::Lt,
        CmpOp::Le,
    ];

    const ACCUM_OPS: &'static [CmpAccumOp] =
        &[CmpAccumOp::None, CmpAccumOp::And, CmpAccumOp::Or];

    const RES_TYPES: &'static [CmpResultType] =
        &[CmpResultType::I1, CmpResultType::F1, CmpResultType::M1];

    let mut a = Acorn::new();
    for &src_type in DATA_TYPES {
        for &cmp_op in CMP_OPS {
            for &accum_op in ACCUM_OPS {
                for &res_type in RES_TYPES {
                    let op = OpICmp {
                        dst: DstRef::None.into(),
                        src_type,
                        res_type,
                        cmp_op,
                        srcs: [0.into(), 0.into()],
                        accum: 0.into(),
                        accum_op,
                    };
                    // Accum is always treated as a bool so let's use 0-1
                    // (otherwise it would always be true)
                    test_foldable_op_with(op, |i| match i {
                        2 => a.get_u32() % 2,
                        _ => a.get_u32(),
                    });
                }
            }
        }
    }
}

#[test]
fn test_op_icmp_multi() {
    const DATA_TYPES: &'static [DataType] = &[DataType::S32, DataType::U32];

    const CMP_OPS: &'static [CmpOp] = &[
        CmpOp::Eq,
        CmpOp::Gt,
        CmpOp::Ge,
        CmpOp::Ne,
        CmpOp::Lt,
        CmpOp::Le,
    ];

    const RES_TYPES: &'static [CmpResultType] = &[
        CmpResultType::I1,
        CmpResultType::F1,
        CmpResultType::M1,
        CmpResultType::C,
    ];

    let mut a = Acorn::new();
    for &src_type in DATA_TYPES {
        for &cmp_op in CMP_OPS {
            for &res_type in RES_TYPES {
                if res_type == CmpResultType::C && src_type == DataType::S32 {
                    continue;
                }

                let op = OpICmpMulti {
                    dst: DstRef::None.into(),
                    src_type,
                    res_type,
                    cmp_op,
                    srcs: [0.into(), 0.into()],
                    accum: 0.into(),
                };
                // Accum should be 0, 1, or -1
                test_foldable_op_with(op, |i| match i {
                    2 => (a.get_u32() % 3) - 1,
                    _ => a.get_u32(),
                });
            }
        }
    }
}

#[test]
fn test_op_idpadd() {
    let model = RunSingleton::get().model.as_ref();

    const SRC_TYPES: &'static [DataType] = &[DataType::V4S8, DataType::V4U8];

    for saturate in [false, true] {
        let op = OpIDpAdd {
            dst: DstRef::None.into(),
            dst_type: DataType::U32,
            saturate,
            src_types: [DataType::V4U8; 2],
            srcs: [0.into(), 0.into()],
            accum: 0.into(),
        };
        test_foldable_op(op);
    }

    for &src0_type in SRC_TYPES {
        for &src1_type in SRC_TYPES {
            let src_types = [src0_type, src1_type];
            if model.arch() < 14 && src_types != [DataType::V4S8; 2] {
                continue;
            }

            for saturate in [false, true] {
                let op = OpIDpAdd {
                    dst: DstRef::None.into(),
                    dst_type: DataType::S32,
                    saturate,
                    src_types,
                    srcs: [0.into(), 0.into()],
                    accum: 0.into(),
                };
                test_foldable_op(op);
            }
        }
    }
}

#[test]
fn test_op_imul() {
    const DATA_TYPES: &'static [DataType] = &[
        DataType::V2S16,
        DataType::V2U16,
        DataType::S32,
        DataType::U32,
    ];

    const WIDENS: &'static [AsmSwizzleWiden] = &[
        AsmSwizzleWiden::None,
        AsmSwizzleWiden::B00,
        AsmSwizzleWiden::B02,
        AsmSwizzleWiden::B20,
        AsmSwizzleWiden::H00,
        AsmSwizzleWiden::H10,
        AsmSwizzleWiden::H0,
        AsmSwizzleWiden::H1,
        // AsmSwizzleWiden::W0, // TODO: 64-bit swizzles
        // AsmSwizzleWiden::W1,
    ];

    for &dst_type in DATA_TYPES {
        for widen in WIDENS {
            let Some(src0_swizzle) = widen.to_swizzle(dst_type) else {
                continue;
            };
            for saturate in [false, true] {
                let op = OpIMul {
                    dst: DstRef::None.into(),
                    srcs: [Src::from(0).swizzle(src0_swizzle), 0.into()],
                    dst_type,
                    saturate,
                };
                test_foldable_op(op);
            }
        }
    }
}

#[test]
fn test_op_isub() {
    const DATA_TYPES: &'static [DataType] = &[
        DataType::V2S16,
        DataType::V2U16,
        DataType::S32,
        DataType::U32,
        DataType::S64,
        DataType::U64,
    ];

    const WIDENS: &'static [AsmSwizzleWiden] = &[
        AsmSwizzleWiden::None,
        AsmSwizzleWiden::B00,
        AsmSwizzleWiden::B02,
        AsmSwizzleWiden::B20,
        AsmSwizzleWiden::H00,
        AsmSwizzleWiden::H10,
        AsmSwizzleWiden::H0,
        AsmSwizzleWiden::H1,
        // AsmSwizzleWiden::W0, // TODO: 64-bit swizzles
        // AsmSwizzleWiden::W1,
    ];

    for &dst_type in DATA_TYPES {
        for widen in WIDENS {
            let Some(src0_swizzle) = widen.to_swizzle(dst_type) else {
                continue;
            };
            for saturate in [false, true] {
                // Not supported by hw
                if saturate && dst_type.bits() == 64 {
                    continue;
                }

                let op = OpISub {
                    dst: DstRef::None.into(),
                    srcs: [Src::from(0).swizzle(src0_swizzle), 0.into()],
                    dst_type,
                    saturate,
                };
                test_foldable_op(op);
            }
        }
    }
}

#[test]
fn test_op_mux() {
    const DATA_TYPES: &'static [DataType] =
        &[DataType::V4I8, DataType::V2I16, DataType::I32];
    const MUX_OPS: &'static [MuxOp] =
        &[MuxOp::Neg, MuxOp::IntZero, MuxOp::FpZero, MuxOp::Bit];

    for &dst_type in DATA_TYPES {
        for &mux_op in MUX_OPS {
            if mux_op == MuxOp::FpZero && dst_type != DataType::I32 {
                continue;
            }
            let op = OpMux {
                dst: DstRef::None.into(),
                dst_type,
                mux_op,
                src0: 0.into(),
                src1: 0.into(),
                sel: 0.into(),
            };
            test_foldable_op(op);
        }
    }
}

#[test]
fn test_op_popcount() {
    let op = OpPopCount {
        dst: DstRef::None.into(),
        src: 0.into(),
    };

    test_foldable_op(op);
}

#[test]
fn test_op_shift_lop() {
    const DATA_TYPES: &'static [DataType] = &[
        DataType::V4U8,
        DataType::V2U16,
        DataType::U32,
        DataType::U64,
    ];

    const SHIFT_OPS: &'static [ShiftOp] = &[
        ShiftOp::None,
        ShiftOp::LShift,
        ShiftOp::RShift,
        ShiftOp::ARShift,
        ShiftOp::RRot,
        ShiftOp::LRot,
    ];

    const LOGIC_OPS: &'static [LogicOp] =
        &[LogicOp::None, LogicOp::Or, LogicOp::And, LogicOp::Xor];

    const WIDENS: &'static [AsmSwizzleWiden] = &[
        AsmSwizzleWiden::None,
        AsmSwizzleWiden::B0,
        AsmSwizzleWiden::H0,
    ];

    for &dst_type in DATA_TYPES {
        for widen in WIDENS {
            let Some(src0_swizzle) = widen.to_swizzle(dst_type) else {
                continue;
            };
            for &shift_op in SHIFT_OPS {
                for &logic_op in LOGIC_OPS {
                    for not_result in [false, true] {
                        let op = OpShiftLop {
                            dst: DstRef::None.into(),
                            dst_type,
                            shift_op,
                            logic_op,
                            not_result,
                            src0: Src::from(0).swizzle(src0_swizzle),
                            shift: 0.into(),
                            src2: 0.into(),
                        };
                        test_foldable_op(op);
                    }
                }
            }
        }
    }
}

mod builder {
    use super::*;

    #[derive(Clone, Copy)]
    enum FPrecision {
        Ulp(u32),
        Abs(f32),
    }

    fn ulp_dist(a: f32, b: f32) -> u32 {
        let ulp_key = |x: f32| {
            let sign_bit = 1 << 31;
            let bits = x.to_bits();
            if bits & sign_bit != 0 {
                !bits
            } else {
                bits ^ sign_bit
            }
        };

        ulp_key(a).abs_diff(ulp_key(b))
    }

    fn cmp_eq_f32(real: f32, expected: f32, prec: FPrecision) -> bool {
        if real.is_nan() || expected.is_nan() {
            return real.is_nan() && expected.is_nan();
        }
        match prec {
            FPrecision::Ulp(ulps) => {
                if (real - expected).abs() < f32::MIN_POSITIVE {
                    return true; // ftz
                }
                ulp_dist(real, expected) < ulps
            }
            FPrecision::Abs(prec) => (real - expected).abs() < prec,
        }
    }

    // `assert_feq!(expected, hardware, prec, "msg {..}")`
    macro_rules! assert_feq {
        ($expected:expr, $hardware:expr, $prec:expr, $($fmt:tt)+) => {{
            if !cmp_eq_f32($expected, $hardware, $prec) {
                panic!(
                    "Test {} failed\nExpected: {}\nHardware: {}",
                    format_args!($($fmt)+), $expected, $hardware
                );
            }
        }};
    }

    fn sample_f32_range(rng: &mut Acorn, range: Range<f32>) -> f32 {
        let t = (rng.get_u32() as f64 / u32::MAX as f64) as f32;
        t * (range.end - range.start) + range.start
    }

    #[test]
    fn test_fexp() {
        // Vulkan Environment for SPIR-V requires an absolute precision of
        // 3 + 2*|x| ULP
        const BASE_RANGE: Range<f32> = 0.0..10.0;
        const RANGE: Range<f32> = -150.0..150.0;

        let run = RunSingleton::get();
        let shader = {
            let mut b = TestShaderBuilder::new(&*run.model);
            let log2_base = b.ld_test_data(0, 32);
            let input = b.ld_test_data(4, 32);

            let dst = b.alloc_ssa(32);
            b.fexp_32_to(dst.into(), input.into(), log2_base.into());
            b.st_test_data(8, dst.into());

            b.compile()
        };

        let mut rng = Acorn::new();
        let exp_case =
            |base: f32, num: f32| [base.log2().to_bits(), num.to_bits(), 0];
        // Notable cases
        let mut data = vec![
            exp_case(1.0, 0.0),
            exp_case(2.0, 0.0),
            exp_case(2.0, 1.0),
            exp_case(2.0, 2.0),
            exp_case(4.0, 2.0),
            exp_case(2.0, -1.0),
            exp_case(2.0, -2.0),
            exp_case(E, 2.0),
        ];

        for _ in 0..1000 {
            let a = sample_f32_range(&mut rng, BASE_RANGE);
            let b = sample_f32_range(&mut rng, RANGE);
            data.push([a.to_bits(), b.to_bits(), 0]);
        }

        let case = shader.with_args(FAU_ONLY_ARGS, &mut data);
        run.execute(case);
        for arr in data {
            let [base_log2, arg, res] = arr.map(f32::from_bits);
            let comp = (base_log2 * arg).exp2();

            let ulps = 3 + 2 * ((base_log2 * arg).abs() as u32);
            let prec = FPrecision::Ulp(ulps);
            assert_feq!(comp, res, prec, "fexp({base_log2}, {arg})");
        }
    }

    #[test]
    fn test_flog2() {
        const RANGE: Range<f32> = -1000.0..1000.0;

        let run = RunSingleton::get();
        let shader = {
            let mut b = TestShaderBuilder::new(&*run.model);
            let input = b.ld_test_data(0, 32);

            let dst = b.flog2_32(input.into());
            b.st_test_data(4, dst.into());
            b.compile()
        };

        let mut rng = Acorn::new();
        let log_case = |arg: f32| [arg.to_bits(), 0];
        // Notable cases
        let mut data = vec![
            log_case(0.5),
            log_case(1.0),
            log_case(2.0),
            log_case(4.0),
            log_case(E),
            log_case(10.0),
            log_case(-1.0),
            log_case(f32::NAN),
        ];

        for _ in 0..1000 {
            let x = sample_f32_range(&mut rng, RANGE);
            data.push([x.to_bits(), 0]);
        }

        let case = shader.with_args(FAU_ONLY_ARGS, &mut data);
        run.execute(case);
        for arr in data {
            let [input, comp] = arr.map(f32::from_bits);
            let res = input.log2();

            let prec = if (0.5..=2.0).contains(&input) {
                FPrecision::Abs(2f32.powi(-21))
            } else {
                FPrecision::Ulp(3)
            };
            assert_feq!(comp, res, prec, "flog2({input})");
        }
    }

    #[test]
    fn test_sin_cos() {
        // Vulkan Environment for SPIR-V requires an absolute precision of 2^-11
        // in the range [-PI, PI]
        const RANGE: Range<f32> = -PI..PI;
        let prec = FPrecision::Abs(2f32.powi(-11));

        let run = RunSingleton::get();
        let shader = {
            let mut b = TestShaderBuilder::new(&*run.model);
            let input = b.ld_test_data(0, 32);

            let sin = b.alloc_ssa(32);
            let cos = b.alloc_ssa(32);
            b.fsincos_32_to(sin.into(), input.clone().into(), false);
            b.fsincos_32_to(cos.into(), input.into(), true);

            b.st_test_data(4, sin.into());
            b.st_test_data(8, cos.into());

            b.compile()
        };

        let mut data = Vec::new();
        let mut rng = Acorn::new();
        // Notable cases
        for c in [0f32, PI, PI / 2.0, PI / 4.0, 2.0 * PI, f32::NAN] {
            data.push([c.to_bits(), 0, 0]);
        }
        for _ in 0..1000 {
            let x = sample_f32_range(&mut rng, RANGE);
            data.push([x.to_bits(), 0, 0]);
        }

        let case = shader.with_args(FAU_ONLY_ARGS, &mut data);
        run.execute(case);
        for arr in data {
            let [input, csin, ccos] = arr.map(f32::from_bits);
            // Rust doesn't specify a precision, but Kraid tests are almost surely
            // built with glibc, that should offer more precision what what we need
            let (esin, ecos) = input.sin_cos();

            assert_feq!(esin, csin, prec, "sin({input})");
            assert_feq!(ecos, ccos, prec, "cos({input})");
        }
    }
}
