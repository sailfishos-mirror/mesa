/*
 * Copyright © 2016-2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "gen_private.h"

#include <gtest/gtest.h>

#include <cstring>

#include "util/ralloc.h"

static const struct intel_gfx_info {
   const char *name;
} gfx_names[] = {
   { "skl", },
   { "bxt", },
   { "kbl", },
   { "aml", },
   { "glk", },
   { "cfl", },
   { "whl", },
   { "cml", },
   { "icl", },
   { "ehl", },
   { "jsl", },
   { "tgl", },
   { "rkl", },
   { "dg1", },
   { "adl", },
   { "sg1", },
   { "rpl", },
   { "dg2", },
   { "mtl", },
   { "lnl", },
   { "bmg", },
   { "ptl", },
};

class gen_validate_test : public ::testing::TestWithParam<struct intel_gfx_info> {
   virtual void SetUp();

public:
   gen_validate_test();
   virtual ~gen_validate_test();

   bool validate(const gen_inst &inst);

   struct intel_device_info devinfo;
};

gen_validate_test::gen_validate_test()
{
   memset(&devinfo, 0, sizeof(devinfo));
}

gen_validate_test::~gen_validate_test()
{
}

bool
gen_validate_test::validate(const gen_inst &inst)
{
   void *mem_ctx = ralloc_context(NULL);

   gen_validate_params params = {};
   params.devinfo = &devinfo;
   params.insts = &inst;
   params.num_insts = 1;
   params.mem_ctx = mem_ctx;

   const bool valid = gen_validate(&params);
   ralloc_free(mem_ctx);

   return valid;
}

void gen_validate_test::SetUp()
{
   struct intel_gfx_info info = GetParam();
   int devid = intel_device_name_to_pci_device_id(info.name);

   intel_get_device_info_from_pci_id(devid, &devinfo);
}

struct gfx_name {
   template <class ParamType>
   std::string
   operator()(const ::testing::TestParamInfo<ParamType>& info) const {
      return info.param.name;
   }
};

INSTANTIATE_TEST_SUITE_P(
   eu_assembly, gen_validate_test,
   ::testing::ValuesIn(gfx_names),
   gfx_name()
);

static gen_inst
make_mov(gen_reg_type dst_type, gen_reg_type src_type,
         unsigned exec_size = 8,
         unsigned dst_subnr = 0,
         unsigned dst_hstride = 1)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_MOV;
   inst.exec_size = exec_size;

   inst.dst.file = GEN_GRF;
   inst.dst.type = dst_type;
   inst.dst.nr = 0;
   inst.dst.subnr = dst_subnr;
   inst.dst.region.hstride = dst_hstride;

   inst.src[0].file = GEN_IMM;
   inst.src[0].type = src_type;
   inst.src[0].imm = 0;

   return inst;
}

static gen_inst
make_rr_mov(gen_reg_type dst_type, gen_reg_type src_type,
            unsigned exec_size = 4)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_MOV;
   inst.exec_size = exec_size;

   inst.dst.file = GEN_GRF;
   inst.dst.type = dst_type;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = src_type;
   inst.src[0].nr = 1;
   inst.src[0].region.vstride = exec_size;
   inst.src[0].region.width = exec_size;
   inst.src[0].region.hstride = 1;

   return inst;
}

static gen_inst
make_add(gen_reg_type type)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_ADD;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = type;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = type;
   inst.src[0].nr = 1;
   inst.src[0].region.vstride = 8;
   inst.src[0].region.width = 8;
   inst.src[0].region.hstride = 1;

   inst.src[1].file = GEN_GRF;
   inst.src[1].type = type;
   inst.src[1].nr = 2;
   inst.src[1].region.vstride = 8;
   inst.src[1].region.width = 8;
   inst.src[1].region.hstride = 1;

   return inst;
}

static gen_inst
make_mul(gen_reg_type dst_type, gen_reg_type src0_type, gen_reg_type src1_type)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_MUL;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = dst_type;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = src0_type;
   inst.src[0].nr = 1;
   inst.src[0].region.vstride = 8;
   inst.src[0].region.width = 8;
   inst.src[0].region.hstride = 1;

   inst.src[1].file = GEN_GRF;
   inst.src[1].type = src1_type;
   inst.src[1].nr = 2;
   inst.src[1].region.vstride = 8;
   inst.src[1].region.width = 8;
   inst.src[1].region.hstride = 1;

   return inst;
}

static gen_inst
make_qword_rule_mul()
{
   gen_inst inst = make_mul(GEN_TYPE_D, GEN_TYPE_D, GEN_TYPE_D);
   inst.exec_size = 8;
   inst.dst.region.hstride = 2;

   for (unsigned i = 0; i < 2; i++) {
      inst.src[i].region.vstride = 8;
      inst.src[i].region.width = 4;
      inst.src[i].region.hstride = 2;
   }

   return inst;
}

static gen_inst
make_math(gen_math func, gen_reg_type type)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_MATH;
   inst.exec_size = 8;
   inst.math.func = func;

   inst.dst.file = GEN_GRF;
   inst.dst.type = type;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = type;
   inst.src[0].nr = 1;
   inst.src[0].region.vstride = 8;
   inst.src[0].region.width = 8;
   inst.src[0].region.hstride = 1;

   inst.src[1].file = GEN_GRF;
   inst.src[1].type = type;
   inst.src[1].nr = 2;
   inst.src[1].region.vstride = 8;
   inst.src[1].region.width = 8;
   inst.src[1].region.hstride = 1;

   return inst;
}

static gen_inst
make_mad(gen_reg_type type)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_MAD;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = type;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   for (unsigned i = 0; i < 3; i++) {
      inst.src[i].file = GEN_GRF;
      inst.src[i].type = type;
      inst.src[i].nr = 1 + i;
      inst.src[i].region.vstride = 8;
      inst.src[i].region.width = 8;
      inst.src[i].region.hstride = 1;
   }

   return inst;
}

static gen_inst
make_add3(gen_reg_type dst_type,
          gen_reg_type src0_type,
          gen_reg_type src1_type,
          gen_reg_type src2_type)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_ADD3;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = dst_type;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = src0_type;
   inst.src[0].nr = 1;

   inst.src[1].file = GEN_GRF;
   inst.src[1].type = src1_type;
   inst.src[1].nr = 2;

   inst.src[2].file = GEN_GRF;
   inst.src[2].type = src2_type;
   inst.src[2].nr = 3;

   return inst;
}

static gen_inst
make_dp4a()
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_DP4A;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = GEN_TYPE_D;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   for (unsigned i = 0; i < 3; i++) {
      inst.src[i].file = GEN_GRF;
      inst.src[i].type = GEN_TYPE_D;
      inst.src[i].nr = 1 + i;
   }

   return inst;
}

static gen_inst
make_dpas(unsigned exec_size = 8)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_DPAS;
   inst.exec_size = exec_size;
   inst.dpas.sdepth = 8;
   inst.dpas.rcount = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = GEN_TYPE_F;
   inst.dst.nr = 0;
   inst.dst.subnr = 0;
   inst.dst.region.hstride = 1;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[0].nr = 1;
   inst.src[0].subnr = 0;

   inst.src[1].file = GEN_GRF;
   inst.src[1].type = GEN_TYPE_HF;
   inst.src[1].nr = 2;
   inst.src[1].subnr = 0;

   inst.src[2].file = GEN_GRF;
   inst.src[2].type = GEN_TYPE_HF;
   inst.src[2].nr = 3;
   inst.src[2].subnr = 0;

   return inst;
}

static gen_inst
make_srnd(gen_file src1_file = GEN_GRF)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_SRND;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = GEN_TYPE_HF;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 2;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[0].nr = 1;
   inst.src[0].region.vstride = 8;
   inst.src[0].region.width = 8;
   inst.src[0].region.hstride = 1;

   inst.src[1].file = src1_file;
   inst.src[1].type = GEN_TYPE_F;
   if (src1_file == GEN_IMM) {
      inst.src[1].imm = 0;
   } else {
      inst.src[1].nr = 2;
      inst.src[1].region.vstride = 8;
      inst.src[1].region.width = 8;
      inst.src[1].region.hstride = 1;
   }

   return inst;
}

static gen_inst
make_add_align16(gen_reg_type type)
{
   gen_inst inst = make_add(type);
   inst.align16 = true;
   inst.dst.region.hstride = 1;

   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;

   inst.src[1].region.vstride = 4;
   inst.src[1].region.width = 4;
   inst.src[1].region.hstride = 1;

   return inst;
}

static gen_inst
make_send(gen_opcode opcode = GEN_OP_SEND)
{
   gen_inst inst = {};
   inst.opcode = opcode;
   inst.exec_size = 16;

   inst.dst = gen_null();
   inst.dst.type = GEN_TYPE_UD;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = GEN_TYPE_UD;
   inst.src[0].nr = 1;

   inst.src[1] = gen_null();
   inst.src[1].type = GEN_TYPE_UD;

   return inst;
}

static gen_inst
make_branch(gen_opcode opcode)
{
   gen_inst inst = {};
   inst.opcode = opcode;
   inst.exec_size = 8;

   switch (opcode) {
   case GEN_OP_JMPI:
   case GEN_OP_BRD:
      inst.src[0] = gen_imm_d(0);
      break;

   case GEN_OP_BRC:
      inst.src[0].file = GEN_GRF;
      inst.src[0].type = GEN_TYPE_D;
      inst.src[0].nr = 1;
      inst.src[0].region.vstride = 2;
      inst.src[0].region.width = 2;
      inst.src[0].region.hstride = 1;
      break;

   default:
      break;
   }

   return inst;
}

struct special_region_dst {
   gen_reg_type type;
   unsigned subnr;
   unsigned hstride;
};

struct special_region_src {
   gen_reg_type type;
   unsigned subnr;
   unsigned vstride;
   unsigned width;
   unsigned hstride;
   bool indirect;
};

static gen_inst
make_special_region_add(const special_region_dst &dst,
                        const special_region_src &src0,
                        const special_region_src &src1)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_ADD;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = dst.type;
   inst.dst.nr = 0;
   inst.dst.subnr = dst.subnr * gen_type_size_bytes(dst.type);
   inst.dst.region.hstride = dst.hstride;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = src0.type;
   inst.src[0].nr = 2;
   inst.src[0].subnr = src0.subnr * gen_type_size_bytes(src0.type);
   inst.src[0].indirect = src0.indirect;
   inst.src[0].region.vstride = src0.vstride;
   inst.src[0].region.width = src0.width;
   inst.src[0].region.hstride = src0.hstride;

   inst.src[1].file = GEN_GRF;
   inst.src[1].type = src1.type;
   inst.src[1].nr = 4;
   inst.src[1].subnr = src1.subnr * gen_type_size_bytes(src1.type);
   inst.src[1].indirect = src1.indirect;
   inst.src[1].region.vstride = src1.vstride;
   inst.src[1].region.width = src1.width;
   inst.src[1].region.hstride = src1.hstride;

   return inst;
}

static uint32_t
make_message_desc(unsigned msg_length,
                  unsigned response_length,
                  bool header_present)
{
   return SET_BITS(msg_length, 28, 25) |
          SET_BITS(response_length, 24, 20) |
          SET_BITS(header_present, 19, 19);
}

TEST_P(gen_validate_test, sanity)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_MOV;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = GEN_TYPE_D;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   inst.src[0].file = GEN_GRF;
   inst.src[0].type = GEN_TYPE_D;
   inst.src[0].nr = 1;
   inst.src[0].region.vstride = 8;
   inst.src[0].region.width = 8;
   inst.src[0].region.hstride = 1;

   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, gfx9_only_opcode_restrictions)
{
   const gen_opcode ops[] = {
      GEN_OP_DP2,
      GEN_OP_DP3,
      GEN_OP_DP4,
      GEN_OP_DPH,
      GEN_OP_LINE,
      GEN_OP_LRP,
      GEN_OP_PLN,
   };

   auto make_opcode_inst = [&](gen_opcode op) {
      switch (op) {
      case GEN_OP_LINE:
      case GEN_OP_PLN: {
         gen_inst inst = make_add(GEN_TYPE_F);
         inst.opcode = op;
         inst.src[0].region.vstride = 0;
         inst.src[0].region.width = 1;
         inst.src[0].region.hstride = 0;
         return inst;
      }

      case GEN_OP_LRP: {
         gen_inst inst = make_mad(GEN_TYPE_F);
         inst.opcode = op;
         inst.align16 = devinfo.ver == 9;
         return inst;
      }

      default: {
         gen_inst inst = make_add(GEN_TYPE_F);
         inst.opcode = op;
         return inst;
      }
      }
   };

   for (gen_opcode op : ops) {
      SCOPED_TRACE(::testing::Message() << "opcode=" << unsigned(op));
      EXPECT_EQ(devinfo.ver == 9, validate(make_opcode_inst(op)));
   }
}

TEST_P(gen_validate_test, sends_wait_version_restrictions)
{
   const gen_opcode send_ops[] = {
      GEN_OP_SENDS,
      GEN_OP_SENDSC,
   };

   for (gen_opcode op : send_ops) {
      SCOPED_TRACE(::testing::Message() << "opcode=" << unsigned(op));
      EXPECT_EQ(devinfo.ver == 9 || devinfo.ver == 11,
                validate(make_send(op)));
   }

   gen_inst wait = make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD, 8);
   wait.opcode = GEN_OP_WAIT;
   EXPECT_EQ(devinfo.ver == 9 || devinfo.ver == 11, validate(wait));
}

TEST_P(gen_validate_test, newer_opcode_version_restrictions)
{
   const struct {
      gen_opcode op;
      bool expected;
   } tests[] = {
      { GEN_OP_ROL,  devinfo.ver >= 11 },
      { GEN_OP_ROR,  devinfo.ver >= 11 },
      { GEN_OP_ADD3, devinfo.ver >= 12 },
      { GEN_OP_BFN,  devinfo.ver >= 12 },
      { GEN_OP_DP4A, devinfo.ver >= 12 },
      { GEN_OP_SYNC, devinfo.ver >= 12 },
      { GEN_OP_DPAS, devinfo.verx10 >= 125 },
      { GEN_OP_SRND, devinfo.ver >= 20 },
   };

   auto make_opcode_inst = [&](gen_opcode op) {
      switch (op) {
      case GEN_OP_ROL:
      case GEN_OP_ROR: {
         gen_inst inst = make_add(GEN_TYPE_UD);
         inst.opcode = op;
         return inst;
      }

      case GEN_OP_ADD3:
         return make_add3(GEN_TYPE_D, GEN_TYPE_D, GEN_TYPE_D, GEN_TYPE_D);

      case GEN_OP_BFN: {
         gen_inst inst = make_mad(GEN_TYPE_UD);
         inst.opcode = op;
         return inst;
      }

      case GEN_OP_DP4A:
         return make_dp4a();

      case GEN_OP_SYNC: {
         gen_inst inst = {};
         inst.opcode = GEN_OP_SYNC;
         inst.exec_size = 1;
         inst.src[0].file = GEN_GRF;
         inst.src[0].type = GEN_TYPE_UD;
         inst.src[0].nr = 1;
         inst.src[0].region.vstride = 0;
         inst.src[0].region.width = 1;
         inst.src[0].region.hstride = 0;
         return inst;
      }

      case GEN_OP_DPAS:
         return make_dpas(devinfo.ver >= 20 ? 16 : 8);

      case GEN_OP_SRND:
         return make_srnd();

      default:
         return gen_inst {};
      }
   };

   for (const auto &test : tests) {
      SCOPED_TRACE(::testing::Message() << "opcode=" << unsigned(test.op));
      EXPECT_EQ(test.expected, validate(make_opcode_inst(test.op)));
   }
}

TEST_P(gen_validate_test, src0_null_reg)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_MOV;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = GEN_TYPE_D;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   inst.src[0].file = GEN_ARF;
   inst.src[0].type = GEN_TYPE_D;
   inst.src[0].nr = GEN_ARF_NULL;

   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, src1_null_reg)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_NULL;

   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, math_src0_null_reg)
{
   gen_inst inst = make_math(GEN_MATH_POW, GEN_TYPE_F);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_NULL;

   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, math_src1_null_reg)
{
   gen_inst inst = make_math(GEN_MATH_POW, GEN_TYPE_F);
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_NULL;

   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, math_single_src_null_reg_allowed)
{
   gen_inst inst = make_math(GEN_MATH_INV, GEN_TYPE_F);
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_NULL;

   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, math_hf_scalar_broadcast_workaround)
{
   const intel_device_info *devinfo_ptr = &devinfo;
   const bool needs_wa = intel_needs_workaround(devinfo_ptr, 22016140776);

   gen_inst inst = make_math(GEN_MATH_SQRT, GEN_TYPE_HF);
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_NULL;
   inst.src[0].region.vstride = 0;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 0;
   EXPECT_EQ(!needs_wa, validate(inst));

   inst = make_math(GEN_MATH_INVM, GEN_TYPE_HF);
   inst.src[1].region.vstride = 0;
   inst.src[1].region.width = 1;
   inst.src[1].region.hstride = 0;
   EXPECT_EQ(!needs_wa, validate(inst));
}

TEST_P(gen_validate_test, branch_restrictions)
{
   gen_inst inst = make_branch(GEN_OP_JMPI);
   EXPECT_TRUE(validate(inst));

   inst = make_branch(GEN_OP_JMPI);
   inst.src[0].file = GEN_BAD_FILE;
   EXPECT_FALSE(validate(inst));

   inst = make_branch(GEN_OP_BRD);
   inst.src[1].file = GEN_IMM;
   inst.src[1].type = GEN_TYPE_D;
   inst.src[1].imm = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_branch(GEN_OP_BRC);
   EXPECT_TRUE(validate(inst));

   inst = make_branch(GEN_OP_BRC);
   inst.src[1].file = GEN_IMM;
   inst.src[1].type = GEN_TYPE_D;
   inst.src[1].imm = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_branch(GEN_OP_BRC);
   inst.src[0].region.width = 1;
   EXPECT_FALSE(validate(inst));

   inst = make_branch(GEN_OP_BRC);
   inst.src[0].file = GEN_IMM;
   inst.src[0].imm = 0;
   inst.src[1].file = GEN_IMM;
   inst.src[1].type = GEN_TYPE_D;
   inst.src[1].imm = 0;
   EXPECT_TRUE(validate(inst));

   inst = make_branch(GEN_OP_BRC);
   inst.src[0].file = GEN_IMM;
   inst.src[0].type = GEN_TYPE_D;
   inst.src[0].imm = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_branch(GEN_OP_BRC);
   inst.src[0].file = GEN_IMM;
   inst.src[0].imm = 0;
   inst.src[1].file = GEN_IMM;
   inst.src[1].type = GEN_TYPE_W;
   inst.src[1].imm = 0;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, execution_size_must_be_legal)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.exec_size = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.exec_size = 3;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.exec_size = 8;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, nop_does_not_require_exec_size)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_NOP;
   inst.exec_size = 0;

   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, grf_register_numbers_are_bounded)
{
   const unsigned max_grf = devinfo.ver >= 20 ? 256 : 128;

   gen_inst inst = make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD, 4);
   inst.dst.nr = max_grf - 1;
   inst.src[0].nr = max_grf - 1;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD, 4);
   inst.dst.nr = max_grf;
   EXPECT_FALSE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD, 4);
   inst.src[0].nr = max_grf;
   EXPECT_FALSE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD, 4);
   inst.dst.indirect = true;
   inst.dst.addr_imm = max_grf;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD, 4);
   inst.src[0].indirect = true;
   inst.src[0].addr_imm = max_grf;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, cond_modifier_not_encodable_for_xe_1src_imm64)
{
   if (!devinfo.has_64bit_float && !devinfo.has_64bit_int)
      GTEST_SKIP();

   const gen_reg_type type = devinfo.has_64bit_int ? GEN_TYPE_UQ : GEN_TYPE_DF;
   const uint64_t imm = devinfo.has_64bit_int ? 0x12111015140a0001ull
                                              : 0x3ff0000000000000ull;

   gen_inst inst = make_mov(type, type, 1);
   inst.src[0].imm = imm;
   inst.cmod = GEN_CONDITION_NONE;
   ASSERT_TRUE(validate(inst));

   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_EQ(devinfo.ver < 12, validate(inst));
}

TEST_P(gen_validate_test, channel_offset_restrictions)
{
   gen_inst inst = make_rr_mov(GEN_TYPE_D, GEN_TYPE_D, 4);
   inst.chan_offset = 4;
   EXPECT_EQ(devinfo.ver < 20, validate(inst));

   inst = make_rr_mov(GEN_TYPE_D, GEN_TYPE_D, 4);
   inst.chan_offset = 8;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_D, GEN_TYPE_D, 8);
   inst.chan_offset = 4;
   EXPECT_EQ(devinfo.ver < 12, validate(inst));
}

TEST_P(gen_validate_test, 3src_inst_access_mode)
{
   if (devinfo.ver >= 12)
      return;

   gen_inst inst = make_mad(GEN_TYPE_D);
   inst.align16 = false;
   EXPECT_EQ(devinfo.ver != 9, validate(inst));

   inst = make_mad(GEN_TYPE_D);
   inst.align16 = true;
   EXPECT_EQ(devinfo.ver == 9, validate(inst));
}

TEST_P(gen_validate_test, three_src_src1_cannot_be_immediate)
{
   gen_inst inst = make_mad(GEN_TYPE_F);
   if (devinfo.ver == 9)
      inst.align16 = true;

   inst.src[1].file = GEN_IMM;
   inst.src[1].type = GEN_TYPE_F;
   inst.src[1].imm = 0;

   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, gfx9_three_src_cannot_use_immediate_sources)
{
   if (devinfo.ver != 9)
      return;

   for (unsigned i : { 0, 2 }) {
      SCOPED_TRACE(::testing::Message() << "source=" << i);

      gen_inst inst = make_mad(GEN_TYPE_F);
      inst.align16 = true;
      inst.src[i].file = GEN_IMM;
      inst.src[i].type = GEN_TYPE_F;
      inst.src[i].imm = 0;

      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, gfx11_bfe_and_csel_cannot_use_immediate_sources)
{
   if (devinfo.ver != 11)
      return;

   for (unsigned i : { 0, 2 }) {
      SCOPED_TRACE(::testing::Message() << "BFE source=" << i);

      gen_inst inst = make_mad(GEN_TYPE_D);
      inst.opcode = GEN_OP_BFE;
      inst.src[i].file = GEN_IMM;
      inst.src[i].type = GEN_TYPE_D;
      inst.src[i].imm = 0;

      EXPECT_FALSE(validate(inst));
   }

   for (unsigned i : { 0, 2 }) {
      SCOPED_TRACE(::testing::Message() << "CSEL source=" << i);

      gen_inst inst = make_mad(GEN_TYPE_F);
      inst.opcode = GEN_OP_CSEL;
      inst.cmod = GEN_CONDITION_ZE;
      inst.src[i].file = GEN_IMM;
      inst.src[i].type = GEN_TYPE_F;
      inst.src[i].imm = 0;

      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, gfx11_mad_allows_at_most_one_immediate_source)
{
   if (devinfo.ver != 11)
      return;

   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.src[0].file = GEN_IMM;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[0].imm = 0;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.src[0].file = GEN_IMM;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[0].imm = 0;
   inst.src[2].file = GEN_IMM;
   inst.src[2].type = GEN_TYPE_F;
   inst.src[2].imm = 0;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, align16_3src_repctrl_must_be_zero_for_64bit_sources)
{
   if (devinfo.ver != 9)
      return;

   if (!devinfo.has_64bit_float)
      return;

   const gen_reg_type type = GEN_TYPE_DF;

   gen_inst inst = make_mad(type);
   inst.align16 = true;
   EXPECT_TRUE(validate(inst));

   for (unsigned i = 0; i < 3; i++) {
      SCOPED_TRACE(::testing::Message() << "source = " << i);

      inst = make_mad(type);
      inst.align16 = true;
      inst.src[i].rep_ctrl = true;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, three_src_must_use_direct_addressing)
{
   gen_inst inst = make_mad(GEN_TYPE_F);
   if (devinfo.ver == 9)
      inst.align16 = true;

   inst.dst.indirect = true;
   EXPECT_FALSE(validate(inst));

   for (unsigned i = 0; i < 3; i++) {
      SCOPED_TRACE(::testing::Message() << "source = " << i);

      inst = make_mad(GEN_TYPE_F);
      if (devinfo.ver == 9)
         inst.align16 = true;
      inst.src[i].indirect = true;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, align16_3src_requires_grf_operands)
{
   if (devinfo.ver != 9)
      return;

   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.align16 = true;
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_ACCUMULATOR;
   EXPECT_FALSE(validate(inst));

   for (unsigned i = 0; i < 3; i++) {
      SCOPED_TRACE(::testing::Message() << "source = " << i);

      inst = make_mad(GEN_TYPE_F);
      inst.align16 = true;
      inst.src[i].file = GEN_ARF;
      inst.src[i].nr = GEN_ARF_ACCUMULATOR;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, align16_3src_destination_type)
{
   if (devinfo.ver != 9)
      return;

   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.align16 = true;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_W);
   inst.align16 = true;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, align16_3src_subregister_encoding)
{
   if (devinfo.ver != 9)
      return;

   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.align16 = true;
   inst.dst.subnr = 4;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.align16 = true;
   inst.dst.subnr = 2;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.align16 = true;
   inst.src[0].subnr = 31;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, align1_3src_destination_file_and_hstride)
{
   if (devinfo.ver == 9)
      return;

   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_ADDRESS;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.dst.region.hstride = 2;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.dst.region.hstride = 4;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, align1_3src_source_region_encoding)
{
   if (devinfo.ver == 9)
      return;

   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.src[0].region.hstride = 3;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.src[2].region.hstride = 3;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.src[0].region.vstride = 3;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, align1_3src_subregister_encoding)
{
   if (devinfo.ver == 9)
      return;

   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.dst.subnr = devinfo.ver >= 20 ? 62 : 31;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.dst.subnr = devinfo.ver >= 20 ? 31 : 32;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.src[0].subnr = devinfo.ver >= 20 ? 64 : 32;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, align1_3src_source_types_match_destination_execution_type)
{
   if (devinfo.ver == 9)
      return;

   gen_inst inst = make_mad(GEN_TYPE_F);
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.src[0].type = GEN_TYPE_D;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_D);
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_D);
   inst.src[2].type = GEN_TYPE_F;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, three_src_register_sources_must_use_register_files)
{
   gen_inst inst = make_mad(GEN_TYPE_F);
   if (devinfo.ver == 9)
      inst.align16 = true;

   inst.src[0].file = GEN_BAD_FILE;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, align16_mode_not_allowed_on_gfx11plus)
{
   gen_inst inst = make_rr_mov(GEN_TYPE_D, GEN_TYPE_D, 4);
   inst.align16 = true;
   inst.dst.region.hstride = 1;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;

   EXPECT_EQ(devinfo.ver < 11, validate(inst));
}

TEST_P(gen_validate_test, dest_stride_must_be_equal_to_the_ratio_of_exec_size_to_dest_size)
{
   gen_inst inst = make_add(GEN_TYPE_W);
   inst.src[0].type = GEN_TYPE_D;
   inst.src[1].type = GEN_TYPE_D;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_W);
   inst.dst.region.hstride = 2;
   inst.src[0].type = GEN_TYPE_D;
   inst.src[1].type = GEN_TYPE_D;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, dst_subreg_must_be_aligned_to_exec_type_size)
{
   gen_inst inst = make_add(GEN_TYPE_W);
   inst.dst.subnr = 2;
   inst.dst.region.hstride = 2;
   inst.src[0].type = GEN_TYPE_D;
   inst.src[1].type = GEN_TYPE_D;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_W);
   inst.exec_size = 4;
   inst.dst.subnr = 8;
   inst.dst.region.hstride = 2;
   inst.src[0].type = GEN_TYPE_D;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;
   inst.src[1].type = GEN_TYPE_D;
   inst.src[1].region.vstride = 4;
   inst.src[1].region.width = 4;
   inst.src[1].region.hstride = 1;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, exec_size_less_than_width)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.src[0].region.width = 16;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.src[1].region.width = 16;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, vertical_stride_is_width_by_horizontal_stride)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.src[0].region.vstride = 4;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.src[1].region.vstride = 4;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, horizontal_stride_must_be_0_if_width_is_1)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.src[0].region.vstride = 0;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 1;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.src[1].region.vstride = 0;
   inst.src[1].region.width = 1;
   inst.src[1].region.hstride = 1;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, scalar_region_must_be_0_1_0)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.exec_size = 1;
   inst.src[0].region.vstride = 1;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.exec_size = 1;
   inst.src[1].region.vstride = 1;
   inst.src[1].region.width = 1;
   inst.src[1].region.hstride = 0;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, zero_stride_implies_0_1_0)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.src[0].region.vstride = 0;
   inst.src[0].region.width = 2;
   inst.src[0].region.hstride = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.src[1].region.vstride = 0;
   inst.src[1].region.width = 2;
   inst.src[1].region.hstride = 0;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, dst_horizontal_stride_0)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.dst.region.hstride = 0;
   EXPECT_FALSE(validate(inst));

   if (devinfo.ver >= 11)
      return;

   inst = make_add_align16(GEN_TYPE_D);
   inst.dst.region.hstride = 0;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, must_not_cross_grf_boundary_in_a_width)
{
   const unsigned packed_crossing_subnr = devinfo.grf_size - 28;
   const unsigned strided_crossing_subnr = devinfo.grf_size - 24;

   gen_inst inst = make_add(GEN_TYPE_D);
   inst.src[0].subnr = packed_crossing_subnr;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.src[1].subnr = packed_crossing_subnr;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.src[0].subnr = strided_crossing_subnr;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 2;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.src[1].subnr = strided_crossing_subnr;
   inst.src[1].region.vstride = 4;
   inst.src[1].region.width = 4;
   inst.src[1].region.hstride = 2;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, dst_hstride_on_align16_must_be_1)
{
   if (devinfo.ver >= 11)
      return;

   gen_inst inst = make_add_align16(GEN_TYPE_D);
   inst.dst.region.hstride = 2;
   EXPECT_FALSE(validate(inst));

   inst = make_add_align16(GEN_TYPE_D);
   inst.dst.region.hstride = 1;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, vstride_on_align16_must_be_0_or_4)
{
   if (devinfo.ver >= 11)
      return;

   const struct {
      unsigned vstride;
      bool expected_result;
   } tests[] = {
      { 0, true  },
      { 1, false },
      { 2, true  },
      { 4, true  },
      { 8, false },
      { 16, false },
      { 32, false },
      { GEN_VSTRIDE_ONE_DIMENSIONAL, false },
   };

   for (const auto &test : tests) {
      SCOPED_TRACE(::testing::Message() << "vstride=" << test.vstride);

      gen_inst inst = make_add_align16(GEN_TYPE_D);
      inst.src[0].region.vstride = test.vstride;
      EXPECT_EQ(test.expected_result, validate(inst));

      inst = make_add_align16(GEN_TYPE_D);
      inst.src[1].region.vstride = test.vstride;
      EXPECT_EQ(test.expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, source_cannot_span_more_than_2_registers)
{
   const gen_reg_type type = devinfo.ver >= 20 ? GEN_TYPE_D : GEN_TYPE_W;

   gen_inst inst = make_add(type);
   inst.exec_size = 32;
   inst.src[1].region.vstride = 16;
   inst.src[1].region.width = 8;
   inst.src[1].region.hstride = 2;
   EXPECT_FALSE(validate(inst));

   inst = make_add(type);
   inst.exec_size = 16;
   inst.src[1].subnr = 2;
   inst.src[1].region.vstride = 16;
   inst.src[1].region.width = 8;
   inst.src[1].region.hstride = 2;
   EXPECT_TRUE(validate(inst));

   inst = make_add(type);
   inst.exec_size = 16;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, destination_cannot_span_more_than_2_registers)
{
   const unsigned invalid_stride = devinfo.ver >= 20 ? 4 : 2;

   gen_inst inst = make_add(GEN_TYPE_W);
   inst.exec_size = 32;
   inst.dst.region.hstride = invalid_stride;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_W);
   inst.exec_size = 8;
   inst.dst.subnr = 6;
   inst.dst.region.hstride = 4;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, src_region_spans_two_regs_dst_region_spans_one)
{
   const gen_reg_type type = devinfo.ver >= 20 ? GEN_TYPE_D : GEN_TYPE_W;

   gen_inst inst = make_add(type);
   inst.src[1].region.vstride = 16;
   inst.src[1].region.width = 4;
   inst.src[1].region.hstride = 2;
   EXPECT_TRUE(validate(inst));

   inst = make_add(type);
   inst.dst.subnr = 16;
   inst.src[1].region.vstride = 16;
   inst.src[1].region.width = 4;
   inst.src[1].region.hstride = 2;
   EXPECT_TRUE(validate(inst));

   inst = make_add(type);
   inst.exec_size = 16;
   inst.src[1].region.vstride = 16;
   inst.src[1].region.width = 8;
   inst.src[1].region.hstride = 2;
   EXPECT_TRUE(validate(inst));

   inst = make_add(type);
   inst.exec_size = 4;
   inst.dst.subnr = 10;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;
   inst.src[1].region.vstride = 16;
   inst.src[1].region.width = 2;
   inst.src[1].region.hstride = 1;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, dst_elements_must_be_evenly_split_between_registers)
{
   gen_inst inst = make_math(GEN_MATH_SIN, GEN_TYPE_F);
   inst.exec_size = devinfo.grf_size / gen_type_size_bytes(GEN_TYPE_F);
   inst.src[0].region.vstride = inst.exec_size;
   inst.src[0].region.width = inst.exec_size;
   inst.src[0].region.hstride = 1;
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_NULL;
   inst.src[1].type = GEN_TYPE_F;
   EXPECT_TRUE(validate(inst));

   inst.dst.subnr = 4;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, two_src_two_dst_source_offsets_must_be_same)
{
   gen_inst inst = make_add(GEN_TYPE_F);
   inst.exec_size = 4;
   inst.dst.region.hstride = 4;
   inst.src[0].subnr = 16;
   inst.src[0].region.vstride = 2;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 0;
   inst.src[1].region.vstride = 4;
   inst.src[1].region.width = 4;
   inst.src[1].region.hstride = 1;
   EXPECT_EQ(devinfo.verx10 < 125, validate(inst));

   inst = make_add(GEN_TYPE_F);
   inst.exec_size = 4;
   inst.dst.region.hstride = 4;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 0;
   inst.src[1].region.vstride = 8;
   inst.src[1].region.width = 2;
   inst.src[1].region.hstride = 1;
   EXPECT_EQ(devinfo.verx10 < 125, validate(inst));
}

TEST_P(gen_validate_test, two_src_two_dst_each_dst_must_be_derived_from_one_src)
{
   gen_inst inst = make_rr_mov(GEN_TYPE_W, GEN_TYPE_W, 16);
   inst.dst.region.hstride = 2;
   inst.src[0].subnr = 8;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_F, GEN_TYPE_F, 8);
   inst.dst.subnr = 16;
   inst.src[0].subnr = 8;
   inst.src[0].region.vstride = 2;
   inst.src[0].region.width = 2;
   inst.src[0].region.hstride = 1;
   EXPECT_EQ(devinfo.verx10 < 125, validate(inst));
}

TEST_P(gen_validate_test, one_src_two_dst)
{
   auto set_scalar_region = [](gen_operand &src) {
      src.region.vstride = 0;
      src.region.width = 1;
      src.region.hstride = 0;
   };

   gen_inst inst = make_add(GEN_TYPE_D);
   inst.exec_size = 16;
   inst.src[0].nr = 0;
   inst.src[1].nr = 0;
   set_scalar_region(inst.src[0]);
   set_scalar_region(inst.src[1]);
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.exec_size = 16;
   inst.dst.type = GEN_TYPE_D;
   inst.src[0].type = GEN_TYPE_W;
   inst.src[1].type = GEN_TYPE_D;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.exec_size = 16;
   inst.dst.type = GEN_TYPE_D;
   inst.src[0].type = GEN_TYPE_D;
   inst.src[1].type = GEN_TYPE_W;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_D);
   inst.exec_size = 16;
   inst.dst.type = GEN_TYPE_D;
   inst.src[0].type = GEN_TYPE_W;
   inst.src[1].type = GEN_TYPE_W;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_W);
   inst.exec_size = 16;
   inst.dst.region.hstride = 2;
   set_scalar_region(inst.src[1]);
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_W);
   inst.exec_size = 16;
   inst.dst.region.hstride = 2;
   set_scalar_region(inst.src[0]);
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, packed_byte_destination)
{
   const struct {
      gen_reg_type dst_type;
      gen_reg_type src_type;
      bool negate;
      bool abs;
      bool saturate;
      bool expected_result;
   } move_tests[] = {
      { GEN_TYPE_UB, GEN_TYPE_UB, false, false, false, true  },
      { GEN_TYPE_B,  GEN_TYPE_B,  false, false, false, true  },
      { GEN_TYPE_UB, GEN_TYPE_B,  false, false, false, true  },
      { GEN_TYPE_B,  GEN_TYPE_UB, false, false, false, true  },
      { GEN_TYPE_UB, GEN_TYPE_UB, true,  false, false, false },
      { GEN_TYPE_UB, GEN_TYPE_UB, false, true,  false, false },
      { GEN_TYPE_UB, GEN_TYPE_UB, false, false, true,  false },
      { GEN_TYPE_UB, GEN_TYPE_UW, false, false, false, false },
      { GEN_TYPE_B,  GEN_TYPE_W,  false, false, false, false },
   };

   for (const auto &test : move_tests) {
      SCOPED_TRACE(::testing::Message()
                   << "dst_type=" << unsigned(test.dst_type)
                   << " src_type=" << unsigned(test.src_type)
                   << " negate=" << test.negate
                   << " abs=" << test.abs
                   << " saturate=" << test.saturate);

      gen_inst inst = make_mov(test.dst_type, test.src_type);
      inst.src[0].file = GEN_GRF;
      inst.src[0].nr = 1;
      inst.src[0].region.vstride = 8;
      inst.src[0].region.width = 8;
      inst.src[0].region.hstride = 1;
      inst.src[0].negate = test.negate;
      inst.src[0].abs = test.abs;
      inst.saturate = test.saturate;

      EXPECT_EQ(test.expected_result, validate(inst));
   }

   gen_inst add = make_add(GEN_TYPE_W);
   add.dst.type = GEN_TYPE_B;
   EXPECT_FALSE(validate(add));
}

TEST_P(gen_validate_test, byte_destination_relaxed_alignment)
{
   gen_inst inst = make_add(GEN_TYPE_W);
   inst.opcode = GEN_OP_SEL;
   inst.pred_control = GEN_PREDICATE_NORMAL;
   inst.dst.type = GEN_TYPE_B;
   inst.dst.region.hstride = 2;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_W);
   inst.opcode = GEN_OP_SEL;
   inst.pred_control = GEN_PREDICATE_NORMAL;
   inst.dst.type = GEN_TYPE_B;
   inst.dst.region.hstride = 2;
   inst.dst.subnr = 1;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, byte_64bit_conversion)
{
   const gen_reg_type types[] = {
      GEN_TYPE_Q,
      GEN_TYPE_UQ,
      GEN_TYPE_DF,
   };

   for (gen_reg_type type : types) {
      SCOPED_TRACE(::testing::Message() << "type=" << unsigned(type));

      gen_inst inst = make_mov(GEN_TYPE_B, type, 8, 0, 2);
      EXPECT_FALSE(validate(inst));

      inst = make_mov(type, GEN_TYPE_B, 8, 0, 1);
      inst.src[0].file = GEN_GRF;
      inst.src[0].nr = 1;
      inst.src[0].region.vstride = 8;
      inst.src[0].region.width = 8;
      inst.src[0].region.hstride = 1;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, half_float_conversion)
{
   gen_inst inst = make_mov(GEN_TYPE_HF, GEN_TYPE_W, 4, 0, 1);
   EXPECT_FALSE(validate(inst));

   inst = make_mov(GEN_TYPE_HF, GEN_TYPE_W, 4, 0, 2);
   EXPECT_TRUE(validate(inst));

   inst = make_mov(GEN_TYPE_HF, GEN_TYPE_W, 4, 2, 2);
   EXPECT_FALSE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_W, GEN_TYPE_HF, 4);
   inst.dst.region.hstride = 1;
   EXPECT_FALSE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_W, GEN_TYPE_HF, 4);
   inst.dst.region.hstride = 2;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_W, GEN_TYPE_HF, 4);
   inst.dst.region.hstride = 2;
   inst.dst.subnr = 2;
   EXPECT_FALSE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_B, GEN_TYPE_HF, 4);
   inst.dst.region.hstride = 4;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_B, GEN_TYPE_HF, 4);
   inst.dst.region.hstride = 4;
   inst.dst.subnr = 1;
   EXPECT_FALSE(validate(inst));

   if (devinfo.has_64bit_int) {
      inst = make_mov(GEN_TYPE_HF, GEN_TYPE_Q, 4, 0, 2);
      EXPECT_FALSE(validate(inst));

      inst = make_rr_mov(GEN_TYPE_Q, GEN_TYPE_HF, 4);
      inst.dst.region.hstride = 1;
      EXPECT_FALSE(validate(inst));
   }

   if (devinfo.has_64bit_float) {
      inst = make_mov(GEN_TYPE_HF, GEN_TYPE_DF, 4, 0, 2);
      EXPECT_FALSE(validate(inst));

      inst = make_rr_mov(GEN_TYPE_DF, GEN_TYPE_HF, 4);
      inst.dst.region.hstride = 1;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_source_indirect_addressing)
{
   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned dst_stride;
      bool dst_indirect;
      bool src0_indirect;
      bool expected_result;
      bool gfx125_expected_result;
   } tests[] = {
      { GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_F,  2, false, false, true,  true  },
      { GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_F,  2, true,  false, true,  true  },
      { GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_F,  2, false, true,  false, false },
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  1, false, false, true,  false },
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  1, true,  false, true,  false },
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  1, false, true,  false, false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  2, false, false, true,  false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 1, false, true,  false, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add(tests[i].dst_type);
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.dst.region.hstride = tests[i].dst_stride;
      inst.dst.indirect = tests[i].dst_indirect;
      inst.src[0].indirect = tests[i].src0_indirect;

      const bool expected = devinfo.verx10 >= 125 ?
         tests[i].gfx125_expected_result : tests[i].expected_result;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_gfx125_region_preservation)
{
   if (devinfo.verx10 < 125)
      GTEST_SKIP();

   gen_inst inst = make_rr_mov(GEN_TYPE_F, GEN_TYPE_HF, 8);
   inst.src[0].region.vstride = 16;
   inst.src[0].region.width = 8;
   inst.src[0].region.hstride = 2;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_F, GEN_TYPE_HF, 8);
   EXPECT_FALSE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_HF, GEN_TYPE_F, 8);
   inst.dst.region.hstride = 2;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_HF, GEN_TYPE_F, 8);
   inst.dst.region.hstride = 1;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, gfx125_float_64bit_explicit_arf_regioning_restrictions)
{
   if (devinfo.verx10 < 125)
      GTEST_SKIP();

   gen_inst inst = make_rr_mov(GEN_TYPE_F, GEN_TYPE_F, 8);
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_F, GEN_TYPE_F, 8);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_F, GEN_TYPE_F, 8);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_FLAG;
   EXPECT_FALSE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_F, GEN_TYPE_F, 8);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_ACCUMULATOR;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_F, GEN_TYPE_F, 8);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_FLAG;
   EXPECT_FALSE(validate(inst));

   if (devinfo.has_64bit_float) {
      inst = make_rr_mov(GEN_TYPE_DF, GEN_TYPE_DF, 4);
      EXPECT_TRUE(validate(inst));

      inst = make_rr_mov(GEN_TYPE_DF, GEN_TYPE_DF, 4);
      inst.src[0].file = GEN_ARF;
      inst.src[0].nr = GEN_ARF_ACCUMULATOR;
      EXPECT_TRUE(validate(inst));

      inst = make_rr_mov(GEN_TYPE_DF, GEN_TYPE_DF, 4);
      inst.src[0].file = GEN_ARF;
      inst.src[0].nr = GEN_ARF_FLAG;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, gfx125_vx1_vxh_indirect_source_restrictions)
{
   if (devinfo.verx10 < 125)
      GTEST_SKIP();

   auto expect_vx1_vxh = [&](gen_inst inst) {
      inst.src[0].indirect = true;
      inst.src[0].addr_imm = 0;
      inst.src[0].region.vstride = inst.exec_size;
      inst.src[0].region.width = inst.exec_size;
      inst.src[0].region.hstride = 1;
      EXPECT_TRUE(validate(inst));

      inst.src[0].region.vstride = GEN_VSTRIDE_ONE_DIMENSIONAL;
      inst.src[0].region.width = inst.exec_size;
      inst.src[0].region.hstride = 1;
      EXPECT_FALSE(validate(inst));

      inst.src[0].region.vstride = GEN_VSTRIDE_ONE_DIMENSIONAL;
      inst.src[0].region.width = 1;
      inst.src[0].region.hstride = 0;
      EXPECT_FALSE(validate(inst));
   };

   expect_vx1_vxh(make_rr_mov(GEN_TYPE_F, GEN_TYPE_F, 8));

   if (devinfo.has_bfloat16)
      expect_vx1_vxh(make_rr_mov(GEN_TYPE_F, GEN_TYPE_BF, 8));

   if (devinfo.has_64bit_float)
      expect_vx1_vxh(make_rr_mov(GEN_TYPE_DF, GEN_TYPE_DF, 4));
}

TEST_P(gen_validate_test, mixed_float_align1_simd16)
{
   const struct {
      unsigned exec_size;
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned dst_stride;
      bool expected_result;
      bool gfx125_expected_result;
   } tests[] = {
      {  8, GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 2, true,  false },
      { 16, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  2, true,  false },
      { 16, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  1, false, false },
      { 16, GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 1, false, false },
      {  8, GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  1, true,  false },
      {  8, GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 1, true,  false },
      { 16, GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  1, false, false },
      { 16, GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 1, false, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add(tests[i].dst_type);
      inst.exec_size = tests[i].exec_size;
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.dst.region.hstride = tests[i].dst_stride;

      const bool expected = devinfo.verx10 >= 125 ?
         tests[i].gfx125_expected_result : tests[i].expected_result;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_align1_packed_fp16_dst_acc_read_offset_0)
{
   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned dst_stride;
      bool read_acc;
      unsigned src0_subnr;
      bool expected_result;
      bool gfx125_expected_result;
   } tests[] = {
      /* Destination is not packed. */
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 2, true,   0, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 2, true,   2, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 2, true,   4, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 2, true,   8, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 2, true,  16, true,  false },

      /* Destination is packed, no accumulator read. */
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, false,  0, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, false,  2, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, false,  4, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, false,  8, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, false, 16, true,  false },

      /* Destination is packed, accumulator read requires source subnr 0. */
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, true,   0, false, false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, true,   2, false, false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, true,   4, false, false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, true,   8, false, false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, true,  16, false, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add(tests[i].dst_type);
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.dst.region.hstride = tests[i].dst_stride;
      if (tests[i].read_acc) {
         inst.src[0].file = GEN_ARF;
         inst.src[0].nr = GEN_ARF_ACCUMULATOR;
      }
      inst.src[0].subnr = tests[i].src0_subnr;

      const bool expected = devinfo.verx10 >= 125 ?
         tests[i].gfx125_expected_result : tests[i].expected_result;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_fp16_dest_with_acc)
{
   const struct {
      unsigned exec_size;
      gen_opcode opcode;
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned dst_stride;
      bool read_acc;
      bool expected_result;
      bool gfx125_expected_result;
   } tests[] = {
      /* Packed fp16 destination with implicit accumulator needs hstride=2. */
      { 8, GEN_OP_MAC, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  1, false, false, false },
      { 8, GEN_OP_MAC, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  2, false, true,  false },
      { 8, GEN_OP_MAC, GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 1, false, false, false },
      { 8, GEN_OP_MAC, GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 2, false, true,  false },

      /* Packed fp16 destination with explicit accumulator needs hstride=2. */
      { 8, GEN_OP_ADD, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  1, true,  false, false },
      { 8, GEN_OP_ADD, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  2, true,  true,  false },
      { 8, GEN_OP_ADD, GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 1, true,  false, false },
      { 8, GEN_OP_ADD, GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 2, true,  true,  false },

      /* If destination is not fp16, the restriction does not apply. */
      { 8, GEN_OP_MAC, GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  1, false, true,  false },
      { 8, GEN_OP_MAC, GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  2, false, true,  false },
      { 8, GEN_OP_ADD, GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  1, false, true,  false },
      { 8, GEN_OP_ADD, GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  2, false, true,  false },

      /* If there is no accumulator source, the restriction does not apply. */
      { 8, GEN_OP_ADD, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  1, false, true,  false },
      { 8, GEN_OP_ADD, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  2, false, true,  false },
      { 8, GEN_OP_ADD, GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 1, false, true,  false },
      { 8, GEN_OP_ADD, GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 2, false, true,  false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add(tests[i].dst_type);
      if (tests[i].opcode == GEN_OP_MAC)
         inst.opcode = GEN_OP_MAC;
      inst.exec_size = tests[i].exec_size;
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.dst.region.hstride = tests[i].dst_stride;
      if (tests[i].read_acc) {
         inst.src[0].file = GEN_ARF;
         inst.src[0].nr = GEN_ARF_ACCUMULATOR;
      }

      const bool expected = devinfo.verx10 >= 125 ?
         tests[i].gfx125_expected_result : tests[i].expected_result;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_src1_accumulator_with_half_float_destination)
{
   const struct {
      unsigned exec_size;
      unsigned dst_stride;
      bool expected_result;
   } tests[] = {
      {  8, 2, true  },
      { 16, 2, true  },
      {  8, 1, false },
      { 16, 1, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add(GEN_TYPE_HF);
      inst.exec_size = tests[i].exec_size;
      inst.dst.type = GEN_TYPE_HF;
      inst.dst.region.hstride = tests[i].dst_stride;

      inst.src[0].type = GEN_TYPE_HF;
      inst.src[0].region.width = 4;
      inst.src[0].region.hstride = 2;
      inst.src[0].region.vstride = inst.src[0].region.width * inst.src[0].region.hstride;

      inst.src[1].file = GEN_ARF;
      inst.src[1].nr = GEN_ARF_ACCUMULATOR;
      inst.src[1].type = GEN_TYPE_F;
      inst.src[1].subnr = 0;
      inst.src[1].region.width = 4;
      inst.src[1].region.hstride = 1;
      inst.src[1].region.vstride = inst.src[1].region.width * inst.src[1].region.hstride;

      EXPECT_EQ(tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_packed_fp16_dst_src1_acc_subnr)
{
   const struct {
      unsigned dst_stride;
      unsigned src1_subnr;
      bool expected_result;
      bool gfx125_expected_result;
   } tests[] = {
      { 1,  0, false, false },
      { 1,  2, false, false },
      { 1,  4, false, false },
      { 1,  8, false, false },
      { 1, 16, false, false },
      { 2,  0, true,  true  },
      { 2,  2, true,  false },
      { 2, 16, true,  false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add(GEN_TYPE_HF);
      inst.exec_size = 8;
      inst.dst.type = GEN_TYPE_HF;
      inst.dst.region.hstride = tests[i].dst_stride;
      inst.dst.subnr = 0;

      inst.src[0].type = GEN_TYPE_HF;
      inst.src[0].region.vstride = 8;
      inst.src[0].region.width = 4;
      inst.src[0].region.hstride = 2;

      inst.src[1].file = GEN_ARF;
      inst.src[1].nr = GEN_ARF_ACCUMULATOR;
      inst.src[1].type = GEN_TYPE_F;
      inst.src[1].subnr = tests[i].src1_subnr;
      inst.src[1].region.vstride = 4;
      inst.src[1].region.width = 4;
      inst.src[1].region.hstride = 1;

      const bool expected = devinfo.verx10 >= 125 ?
         tests[i].gfx125_expected_result : tests[i].expected_result;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_align1_math_packed_fp16_destination)
{
   const struct {
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned src0_stride;
      unsigned src1_stride;
      bool expected_result;
      bool gfx125_expected_result;
   } tests[] = {
      { GEN_TYPE_HF, GEN_TYPE_F,  2, 1, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_F,  1, 1, false, false },
      { GEN_TYPE_F,  GEN_TYPE_HF, 1, 2, true,  false },
      { GEN_TYPE_F,  GEN_TYPE_HF, 1, 1, false, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_math(GEN_MATH_POW, GEN_TYPE_HF);
      inst.dst.type = GEN_TYPE_HF;
      inst.dst.region.hstride = 1;
      inst.dst.subnr = 0;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;

      inst.src[0].region.vstride = 4;
      inst.src[0].region.width = 4;
      inst.src[0].region.hstride = tests[i].src0_stride;
      inst.src[1].region.vstride = 4;
      inst.src[1].region.width = 4;
      inst.src[1].region.hstride = tests[i].src1_stride;

      const bool expected = devinfo.verx10 >= 125 ?
         tests[i].gfx125_expected_result : tests[i].expected_result;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_align1_math_strided_fp16_inputs)
{
   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned dst_stride;
      unsigned src0_stride;
      unsigned src1_stride;
      bool expected_result;
      bool gfx125_expected_result;
   } tests[] = {
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  2, 2, 1, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 2, 1, 2, true,  false },
      { GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 2, 1, 1, false, false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  1, 1, 1, false, false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 1, 1, 2, true,  false },
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_HF, 1, 2, 1, false, false },
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_HF, 1, 2, 2, true,  false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_math(GEN_MATH_POW, tests[i].dst_type);
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.dst.region.hstride = tests[i].dst_stride;
      inst.src[0].region.vstride = 4;
      inst.src[0].region.width = 4;
      inst.src[0].region.hstride = tests[i].src0_stride;
      inst.src[1].region.vstride = 4;
      inst.src[1].region.width = 4;
      inst.src[1].region.hstride = tests[i].src1_stride;

      const bool expected = devinfo.verx10 >= 125 ?
         tests[i].gfx125_expected_result : tests[i].expected_result;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_math_single_hf_source_striding)
{
   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned src0_stride;
      unsigned src1_stride;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  2, 1, true  },
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  1, 1, false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 1, 2, true  },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 1, 1, false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  2, 1, true  },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F,  1, 1, false },
      { GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 1, 2, true  },
      { GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, 1, 1, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_math(GEN_MATH_POW, tests[i].dst_type);
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.dst.region.hstride = tests[i].dst_type == GEN_TYPE_HF ? 2 : 1;

      inst.src[0].region.width = 4;
      inst.src[0].region.hstride = tests[i].src0_stride;
      inst.src[0].region.vstride = inst.src[0].region.width * tests[i].src0_stride;

      inst.src[1].region.width = 4;
      inst.src[1].region.hstride = tests[i].src1_stride;
      inst.src[1].region.vstride = inst.src[1].region.width * tests[i].src1_stride;

      const bool expected = devinfo.verx10 >= 125 ? false : tests[i].expected_result;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_align1_packed_fp16_dst)
{
   const struct {
      unsigned exec_size;
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned dst_stride;
      unsigned dst_subnr;
      bool expected_result;
      bool gfx125_expected_result;
   } tests[] = {
      {  8, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1,  0, true,  false },
      {  8, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1,  2, false, false },
      {  8, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1,  4, false, false },
      {  8, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1,  8, false, false },
      {  8, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, 16, true,  false },
      { 16, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1,  0, false, false },
      { 16, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1,  2, false, false },
      { 16, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1,  4, false, false },
      { 16, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1,  8, false, false },
      { 16, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 1, 16, false, false },
      {  8, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_F, 2,  0, true,  false },
      {  8, GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F, 1,  0, true,  false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add(tests[i].dst_type);
      inst.exec_size = tests[i].exec_size;
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.dst.region.hstride = tests[i].dst_stride;
      inst.dst.subnr = tests[i].dst_subnr;
      inst.src[0].region.vstride = 4;
      inst.src[0].region.width = 4;
      inst.src[0].region.hstride = 1;
      inst.src[1].region.vstride = 4;
      inst.src[1].region.width = 4;
      inst.src[1].region.hstride = 1;

      const bool expected = devinfo.verx10 >= 125 ?
         tests[i].gfx125_expected_result : tests[i].expected_result;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_align16_packed_data)
{
   if (devinfo.ver >= 11)
      GTEST_SKIP();

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned src0_vstride;
      unsigned src1_vstride;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, 4, 4, true  },
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, 2, 4, false },
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, 4, 2, false },
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, 0, 4, false },
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, 4, 0, false },
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  4, 4, true  },
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  4, 2, false },
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  2, 4, false },
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  0, 4, false },
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  4, 0, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add_align16(tests[i].dst_type);
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.src[0].region.vstride = tests[i].src0_vstride;
      inst.src[1].region.vstride = tests[i].src1_vstride;
      EXPECT_EQ(tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_align16_no_simd16)
{
   if (devinfo.ver >= 11)
      GTEST_SKIP();

   const struct {
      unsigned exec_size;
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      bool expected_result;
   } tests[] = {
      {  8, GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, true  },
      {  8, GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  true  },
      { 16, GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, false },
      { 16, GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add_align16(tests[i].dst_type);
      inst.exec_size = tests[i].exec_size;
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.src[0].region.vstride = 4;
      inst.src[1].region.vstride = 4;
      EXPECT_EQ(tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_align16_no_acc_read)
{
   if (devinfo.ver >= 11)
      GTEST_SKIP();

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      bool read_acc;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, false, true  },
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, true,  false },
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  false, true  },
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  true,  false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add_align16(tests[i].dst_type);
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.src[0].region.vstride = 4;
      inst.src[1].region.vstride = 4;
      if (tests[i].read_acc) {
         inst.src[0].file = GEN_ARF;
         inst.src[0].nr = GEN_ARF_ACCUMULATOR;
      }
      EXPECT_EQ(tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_align16_no_acc_read_src1)
{
   if (devinfo.ver >= 11)
      GTEST_SKIP();

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      bool read_acc;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, false, true  },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, true,  false },
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  false, true  },
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  true,  false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_add_align16(tests[i].dst_type);
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.src[0].region.vstride = 4;
      inst.src[1].region.vstride = 4;
      if (tests[i].read_acc) {
         inst.src[1].file = GEN_ARF;
         inst.src[1].nr = GEN_ARF_ACCUMULATOR;
      }
      EXPECT_EQ(tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_float_align16_math_packed_format)
{
   if (devinfo.ver >= 11)
      GTEST_SKIP();

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned src0_vstride;
      unsigned src1_vstride;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_F,  4, 0, false },
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_HF, 4, 4, true  },
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, 4, 0, false },
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, 2, 4, false },
      { GEN_TYPE_F, GEN_TYPE_F,  GEN_TYPE_HF, 4, 2, false },
      { GEN_TYPE_F, GEN_TYPE_HF, GEN_TYPE_HF, 0, 4, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_math(GEN_MATH_POW, tests[i].dst_type);
      inst.align16 = true;
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.src[0].region.vstride = tests[i].src0_vstride;
      inst.src[0].region.width = 4;
      inst.src[0].region.hstride = 1;
      inst.src[1].region.vstride = tests[i].src1_vstride;
      inst.src[1].region.width = 4;
      inst.src[1].region.hstride = 1;
      EXPECT_EQ(tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, bfloat_restrictions)
{
   if (!devinfo.has_bfloat16)
      return;

   const unsigned half_offset = devinfo.grf_size / 2;

   auto set_mov_src_grf = [](gen_inst &inst) {
      inst.src[0].file = GEN_GRF;
      inst.src[0].nr = 1;
      inst.src[0].region.vstride = 8;
      inst.src[0].region.width = 8;
      inst.src[0].region.hstride = 1;
   };

   gen_inst inst = make_mov(GEN_TYPE_BF, GEN_TYPE_F, 8, 0, 2);
   set_mov_src_grf(inst);
   EXPECT_TRUE(validate(inst));

   inst = make_mov(GEN_TYPE_BF, GEN_TYPE_BF, 8, 0, 2);
   set_mov_src_grf(inst);
   EXPECT_FALSE(validate(inst));

   inst = make_mov(GEN_TYPE_BF, GEN_TYPE_F, devinfo.ver < 20 ? 16 : 32, 0, 2);
   set_mov_src_grf(inst);
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.dst.region.hstride = 2;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[1].type = GEN_TYPE_F;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.dst.region.hstride = 2;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.dst.region.hstride = 2;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[1].region.vstride = 0;
   inst.src[1].region.width = 1;
   inst.src[1].region.hstride = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_BF, GEN_TYPE_BF, GEN_TYPE_F);
   EXPECT_TRUE(validate(inst));

   inst = make_mul(GEN_TYPE_BF, GEN_TYPE_F, GEN_TYPE_BF);
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_BF);
   inst.src[2].type = GEN_TYPE_F;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_BF);
   inst.src[1].type = GEN_TYPE_F;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.src[1].type = GEN_TYPE_F;
   inst.dst.subnr = half_offset;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.src[1].type = GEN_TYPE_F;
   inst.dst.subnr = 2;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.dst.region.hstride = 2;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[1].type = GEN_TYPE_F;
   inst.dst.subnr = 4;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.src[1].type = GEN_TYPE_F;
   inst.src[0].region.vstride = 2;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 2;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.src[1].type = GEN_TYPE_F;
   inst.src[0].subnr = half_offset;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.src[1].type = GEN_TYPE_F;
   inst.src[0].subnr = 4;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, mixed_bfloat_destination_offset_edges)
{
   if (!devinfo.has_bfloat16)
      GTEST_SKIP();

   const unsigned half_offset = devinfo.grf_size / 2;

   auto make_bfloat_mov = [](unsigned dst_subnr) {
      gen_inst inst = make_mov(GEN_TYPE_BF, GEN_TYPE_F, 8, dst_subnr, 2);
      inst.src[0].file = GEN_GRF;
      inst.src[0].nr = 1;
      inst.src[0].region.vstride = 8;
      inst.src[0].region.width = 8;
      inst.src[0].region.hstride = 1;
      return inst;
   };

   gen_inst inst = make_bfloat_mov(0);
   EXPECT_TRUE(validate(inst));

   inst = make_bfloat_mov(gen_type_size_bytes(GEN_TYPE_BF));
   EXPECT_TRUE(validate(inst));

   inst = make_bfloat_mov(half_offset);
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, mixed_bfloat_execution_size_boundaries)
{
   if (!devinfo.has_bfloat16)
      GTEST_SKIP();

   auto make_mixed_bfloat_add = [&](unsigned exec_size) {
      gen_inst inst = make_add(GEN_TYPE_BF);
      inst.exec_size = exec_size;
      inst.dst.region.hstride = 2;
      inst.src[0].type = GEN_TYPE_F;
      inst.src[1].type = GEN_TYPE_F;
      inst.src[0].region.vstride = exec_size;
      inst.src[0].region.width = exec_size;
      inst.src[0].region.hstride = 1;
      inst.src[1].region.vstride = exec_size;
      inst.src[1].region.width = exec_size;
      inst.src[1].region.hstride = 1;
      return inst;
   };

   gen_inst inst = make_mixed_bfloat_add(8);
   EXPECT_TRUE(validate(inst));

   inst = make_mixed_bfloat_add(16);
   EXPECT_EQ(devinfo.ver >= 20, validate(inst));

   inst = make_mixed_bfloat_add(32);
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, mixed_bfloat_multiplier_source_indices)
{
   if (!devinfo.has_bfloat16)
      GTEST_SKIP();

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      bool expected_result;
   } mul_tests[] = {
      { GEN_TYPE_BF, GEN_TYPE_BF, GEN_TYPE_F,  true  },
      { GEN_TYPE_BF, GEN_TYPE_F,  GEN_TYPE_BF, false },
      { GEN_TYPE_F,  GEN_TYPE_BF, GEN_TYPE_F,  true  },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_BF, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(mul_tests); i++) {
      SCOPED_TRACE(::testing::Message() << "mul test vector index = " << i);

      gen_inst inst = make_mul(mul_tests[i].dst_type,
                               mul_tests[i].src0_type,
                               mul_tests[i].src1_type);
      EXPECT_EQ(mul_tests[i].expected_result, validate(inst));
   }

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      gen_reg_type src2_type;
      bool expected_result;
   } mad_tests[] = {
      { GEN_TYPE_F,  GEN_TYPE_BF, GEN_TYPE_F,  GEN_TYPE_F,  true  },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_BF, GEN_TYPE_F,  true  },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_BF, false },
      { GEN_TYPE_BF, GEN_TYPE_BF, GEN_TYPE_BF, GEN_TYPE_F,  true  },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(mad_tests); i++) {
      SCOPED_TRACE(::testing::Message() << "mad test vector index = " << i);

      gen_inst inst = make_mad(mad_tests[i].dst_type);
      if (devinfo.ver == 9)
         inst.align16 = true;
      inst.dst.type = mad_tests[i].dst_type;
      inst.src[0].type = mad_tests[i].src0_type;
      inst.src[1].type = mad_tests[i].src1_type;
      inst.src[2].type = mad_tests[i].src2_type;
      EXPECT_EQ(mad_tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, mixed_bfloat_source1_offset_and_packing)
{
   if (!devinfo.has_bfloat16)
      GTEST_SKIP();

   const unsigned half_offset = devinfo.grf_size / 2;

   gen_inst inst = make_add(GEN_TYPE_BF);
   inst.dst.region.hstride = 2;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[1].type = GEN_TYPE_BF;
   inst.src[1].subnr = half_offset;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.dst.region.hstride = 2;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[1].type = GEN_TYPE_BF;
   inst.src[1].subnr = 4;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_BF);
   inst.dst.region.hstride = 2;
   inst.src[0].type = GEN_TYPE_F;
   inst.src[1].type = GEN_TYPE_BF;
   inst.src[1].region.vstride = 2;
   inst.src[1].region.width = 1;
   inst.src[1].region.hstride = 2;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, gfx11_no_byte_src_1_2)
{
   gen_inst add = make_add(GEN_TYPE_D);
   add.src[1].type = GEN_TYPE_B;
   EXPECT_EQ(devinfo.ver < 11, validate(add));

   gen_inst mad = make_mad(GEN_TYPE_D);
   if (devinfo.ver == 9)
      mad.align16 = true;
   mad.src[2].type = GEN_TYPE_B;
   EXPECT_EQ(devinfo.ver < 11, validate(mad));

   gen_inst dpas = make_dpas(devinfo.ver < 20 ? 8 : 16);
   dpas.dst.type = GEN_TYPE_D;
   dpas.src[0].type = GEN_TYPE_D;
   dpas.src[1].type = GEN_TYPE_B;
   dpas.src[2].type = GEN_TYPE_B;
   EXPECT_EQ(devinfo.verx10 >= 125, validate(dpas));
}

TEST_P(gen_validate_test, srnd_type_and_immediate_restrictions)
{
   if (devinfo.ver < 20)
      return;

   gen_inst inst = make_srnd();
   EXPECT_TRUE(validate(inst));

   inst = make_srnd(GEN_IMM);
   EXPECT_TRUE(validate(inst));

   inst = make_srnd();
   inst.src[1].type = GEN_TYPE_UW;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, add3_source_types)
{
   if (devinfo.verx10 < 125)
      return;

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      gen_reg_type src2_type;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_F,  false },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_HF, false },
      { GEN_TYPE_B,  GEN_TYPE_B,  GEN_TYPE_B,  GEN_TYPE_B,  false },
      { GEN_TYPE_UB, GEN_TYPE_UB, GEN_TYPE_UB, GEN_TYPE_UB, false },

      { GEN_TYPE_W,  GEN_TYPE_W,  GEN_TYPE_W,  GEN_TYPE_W,  true  },
      { GEN_TYPE_UW, GEN_TYPE_UW, GEN_TYPE_UW, GEN_TYPE_UW, true  },
      { GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_D,  true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UD, true  },

      { GEN_TYPE_W,  GEN_TYPE_D,  GEN_TYPE_W,  GEN_TYPE_W,  true  },
      { GEN_TYPE_UW, GEN_TYPE_UW, GEN_TYPE_UD, GEN_TYPE_UW, true  },
      { GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_W,  GEN_TYPE_D,  true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UW, true  },
   };

   for (const auto &test : tests) {
      SCOPED_TRACE(::testing::Message()
                   << "dst_type=" << unsigned(test.dst_type)
                   << " src0_type=" << unsigned(test.src0_type)
                   << " src1_type=" << unsigned(test.src1_type)
                   << " src2_type=" << unsigned(test.src2_type));

      const gen_inst inst = make_add3(test.dst_type, test.src0_type,
                                      test.src1_type, test.src2_type);
      EXPECT_EQ(test.expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, add3_immediate_types)
{
   if (devinfo.verx10 < 125)
      return;

   const struct {
      gen_reg_type reg_type;
      gen_reg_type imm_type;
      unsigned imm_src;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_W,  GEN_TYPE_W,  0, true  },
      { GEN_TYPE_W,  GEN_TYPE_W,  2, true  },
      { GEN_TYPE_UW, GEN_TYPE_UW, 0, true  },
      { GEN_TYPE_UW, GEN_TYPE_UW, 2, true  },
      { GEN_TYPE_D,  GEN_TYPE_W,  0, true  },
      { GEN_TYPE_UD, GEN_TYPE_W,  2, true  },
      { GEN_TYPE_D,  GEN_TYPE_UW, 0, true  },
      { GEN_TYPE_UW, GEN_TYPE_UW, 2, true  },

      { GEN_TYPE_W,  GEN_TYPE_D,  0, false },
      { GEN_TYPE_W,  GEN_TYPE_D,  2, false },
      { GEN_TYPE_UW, GEN_TYPE_UD, 0, false },
      { GEN_TYPE_UW, GEN_TYPE_UD, 2, false },
      { GEN_TYPE_D,  GEN_TYPE_D,  0, false },
      { GEN_TYPE_UD, GEN_TYPE_D,  2, false },
      { GEN_TYPE_D,  GEN_TYPE_UD, 0, false },
      { GEN_TYPE_UW, GEN_TYPE_UD, 2, false },
   };

   for (const auto &test : tests) {
      SCOPED_TRACE(::testing::Message()
                   << "reg_type=" << unsigned(test.reg_type)
                   << " imm_type=" << unsigned(test.imm_type)
                   << " imm_src=" << test.imm_src);

      gen_inst inst = make_add3(test.reg_type, test.reg_type,
                                test.reg_type, test.reg_type);
      inst.src[test.imm_src].file = GEN_IMM;
      inst.src[test.imm_src].type = test.imm_type;
      inst.src[test.imm_src].imm = test.imm_src == 0 ? 0x1234 : 0x2143;

      EXPECT_EQ(test.expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, dp4a_accumulator_restrictions)
{
   if (devinfo.ver < 12)
      return;

   gen_inst inst = make_dp4a();
   EXPECT_TRUE(validate(inst));

   inst = make_dp4a();
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_TRUE(validate(inst));

   inst = make_dp4a();
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_TRUE(validate(inst));

   inst = make_dp4a();
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, dpas_sdepth)
{
   if (!devinfo.has_systolic)
      return;

   const unsigned valid_exec_size = devinfo.ver >= 20 ? 16 : 8;
   const unsigned sdepths[] = { 16, 2, 4, 8 };

   for (const unsigned sdepth : sdepths) {
      SCOPED_TRACE(::testing::Message() << "sdepth=" << sdepth);

      gen_inst inst = make_dpas(valid_exec_size);
      inst.dpas.sdepth = sdepth;
      EXPECT_EQ(sdepth == 8, validate(inst));
   }
}

TEST_P(gen_validate_test, dpas_exec_size)
{
   if (!devinfo.has_systolic)
      return;

   const unsigned valid_exec_size = devinfo.ver >= 20 ? 16 : 8;
   const unsigned exec_sizes[] = { 1, 2, 4, 8, 16, 32 };

   for (const unsigned exec_size : exec_sizes) {
      SCOPED_TRACE(::testing::Message() << "exec_size=" << exec_size);

      const gen_inst inst = make_dpas(exec_size);
      EXPECT_EQ(exec_size == valid_exec_size, validate(inst));
   }
}

TEST_P(gen_validate_test, dpas_file_restrictions)
{
   if (!devinfo.has_systolic)
      return;

   const unsigned valid_exec_size = devinfo.ver >= 20 ? 16 : 8;

   gen_inst inst = make_dpas(valid_exec_size);
   EXPECT_TRUE(validate(inst));

   inst = make_dpas(valid_exec_size);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_TRUE(validate(inst));

   inst = make_dpas(valid_exec_size);
   inst.src[0].file = GEN_IMM;
   inst.src[0].imm = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_dpas(valid_exec_size);
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_FALSE(validate(inst));

   inst = make_dpas(valid_exec_size);
   inst.src[2].file = GEN_ARF;
   inst.src[2].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, dpas_atomic_control)
{
   if (!devinfo.has_systolic)
      return;

   const unsigned valid_exec_size = devinfo.ver >= 20 ? 16 : 8;

   gen_inst inst = make_dpas(valid_exec_size);
   EXPECT_TRUE(validate(inst));

   inst = make_dpas(valid_exec_size);
   inst.atomic_control = true;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, dpas_sub_byte_precision)
{
   if (!devinfo.has_systolic)
      return;

   const unsigned valid_exec_size = devinfo.ver >= 20 ? 16 : 8;

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      unsigned src1_subbyte;
      gen_reg_type src2_type;
      unsigned src2_subbyte;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 0, GEN_TYPE_HF, 0, true  },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 0, GEN_TYPE_HF, 1, false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 0, GEN_TYPE_HF, 2, false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 1, GEN_TYPE_HF, 0, false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 2, GEN_TYPE_HF, 0, false },

      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 0, GEN_TYPE_UB, 0, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 0, GEN_TYPE_UB, 1, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 0, GEN_TYPE_UB, 2, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 0, GEN_TYPE_UB, 3, false },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 1, GEN_TYPE_UB, 0, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 2, GEN_TYPE_UB, 0, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 3, GEN_TYPE_UB, 0, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_dpas(valid_exec_size);
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.src[2].type = tests[i].src2_type;
      inst.dpas.src1_subbyte = tests[i].src1_subbyte;
      inst.dpas.src2_subbyte = tests[i].src2_subbyte;

      EXPECT_EQ(tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, dpas_types)
{
   if (!devinfo.has_systolic)
      return;

   const unsigned valid_exec_size = devinfo.ver >= 20 ? 16 : 8;

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      gen_reg_type src2_type;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_HF, true  },
      { GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_HF, devinfo.ver >= 20 },
      { GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_HF, GEN_TYPE_HF, devinfo.ver >= 20 },
      { GEN_TYPE_HF, GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_HF, devinfo.ver >= 20 },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, GEN_TYPE_F,  false },

      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_BF, GEN_TYPE_BF, true  },
      { GEN_TYPE_BF, GEN_TYPE_BF, GEN_TYPE_BF, GEN_TYPE_BF, devinfo.ver >= 20 },
      { GEN_TYPE_BF, GEN_TYPE_F,  GEN_TYPE_BF, GEN_TYPE_BF, devinfo.ver >= 20 },
      { GEN_TYPE_F,  GEN_TYPE_BF, GEN_TYPE_BF, GEN_TYPE_BF, devinfo.ver >= 20 },

      { GEN_TYPE_DF, GEN_TYPE_DF, GEN_TYPE_DF, GEN_TYPE_DF, false },
      { GEN_TYPE_DF, GEN_TYPE_DF, GEN_TYPE_DF, GEN_TYPE_F,  false },
      { GEN_TYPE_DF, GEN_TYPE_DF, GEN_TYPE_F,  GEN_TYPE_DF, false },
      { GEN_TYPE_DF, GEN_TYPE_F,  GEN_TYPE_DF, GEN_TYPE_DF, false },
      { GEN_TYPE_DF, GEN_TYPE_DF, GEN_TYPE_DF, GEN_TYPE_HF, false },
      { GEN_TYPE_DF, GEN_TYPE_DF, GEN_TYPE_HF, GEN_TYPE_DF, false },
      { GEN_TYPE_DF, GEN_TYPE_HF, GEN_TYPE_DF, GEN_TYPE_DF, false },

      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, GEN_TYPE_UB, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, GEN_TYPE_UD, false },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, false },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, GEN_TYPE_UW, false },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UW, GEN_TYPE_UB, false },

      { GEN_TYPE_UD, GEN_TYPE_UB, GEN_TYPE_UB, GEN_TYPE_UB, false },
      { GEN_TYPE_UD, GEN_TYPE_UW, GEN_TYPE_UB, GEN_TYPE_UB, false },

      { GEN_TYPE_UQ, GEN_TYPE_UQ, GEN_TYPE_UB, GEN_TYPE_UB, false },
      { GEN_TYPE_UQ, GEN_TYPE_UQ, GEN_TYPE_UB, GEN_TYPE_UQ, false },
      { GEN_TYPE_UQ, GEN_TYPE_UQ, GEN_TYPE_UQ, GEN_TYPE_UB, false },
      { GEN_TYPE_UQ, GEN_TYPE_UQ, GEN_TYPE_UB, GEN_TYPE_UW, false },
      { GEN_TYPE_UQ, GEN_TYPE_UQ, GEN_TYPE_UW, GEN_TYPE_UB, false },

      { GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_B,  GEN_TYPE_B,  true  },
      { GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_B,  GEN_TYPE_UB, true  },
      { GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_UB, GEN_TYPE_B,  true  },
      { GEN_TYPE_D,  GEN_TYPE_UD, GEN_TYPE_B,  GEN_TYPE_B,  true  },

      { GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_B,  GEN_TYPE_D,  false },
      { GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_B,  false },
      { GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_B,  GEN_TYPE_W,  false },
      { GEN_TYPE_D,  GEN_TYPE_D,  GEN_TYPE_W,  GEN_TYPE_B,  false },

      { GEN_TYPE_D,  GEN_TYPE_B,  GEN_TYPE_B,  GEN_TYPE_B,  false },
      { GEN_TYPE_D,  GEN_TYPE_W,  GEN_TYPE_B,  GEN_TYPE_B,  false },

      { GEN_TYPE_Q,  GEN_TYPE_Q,  GEN_TYPE_B,  GEN_TYPE_B,  false },
      { GEN_TYPE_Q,  GEN_TYPE_Q,  GEN_TYPE_B,  GEN_TYPE_Q,  false },
      { GEN_TYPE_Q,  GEN_TYPE_Q,  GEN_TYPE_Q,  GEN_TYPE_B,  false },
      { GEN_TYPE_Q,  GEN_TYPE_Q,  GEN_TYPE_B,  GEN_TYPE_W,  false },
      { GEN_TYPE_Q,  GEN_TYPE_Q,  GEN_TYPE_W,  GEN_TYPE_B,  false },

      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, GEN_TYPE_B,  false },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_B,  GEN_TYPE_UB, false },
      { GEN_TYPE_UD, GEN_TYPE_D,  GEN_TYPE_UB, GEN_TYPE_UB, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_dpas(valid_exec_size);
      inst.dst.type = tests[i].dst_type;
      inst.src[0].type = tests[i].src0_type;
      inst.src[1].type = tests[i].src1_type;
      inst.src[2].type = tests[i].src2_type;

      bool expected_result = tests[i].expected_result;
      const bool uses_bfloat =
         tests[i].dst_type == GEN_TYPE_BF ||
         tests[i].src0_type == GEN_TYPE_BF ||
         tests[i].src1_type == GEN_TYPE_BF ||
         tests[i].src2_type == GEN_TYPE_BF;
      if (uses_bfloat && !devinfo.has_bfloat16)
         expected_result = false;

      EXPECT_EQ(expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, dpas_src_subreg_nr)
{
   if (!devinfo.has_systolic)
      return;

   const unsigned valid_exec_size = devinfo.ver >= 20 ? 16 : 8;

   const struct {
      gen_reg_type dst_type;
      unsigned dst_subnr;
      gen_reg_type src0_type;
      unsigned src0_subnr;
      gen_reg_type src1_type;
      unsigned src1_subnr;
      gen_reg_type src2_type;
      unsigned src2_subnr;
      unsigned src1_subbyte;
      unsigned src2_subbyte;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F,  0, GEN_TYPE_F,  0, GEN_TYPE_HF, 0, GEN_TYPE_HF,  0, 0, 0, true  },
      { GEN_TYPE_D,  0, GEN_TYPE_D,  0, GEN_TYPE_B,  0, GEN_TYPE_B,   0, 0, 0, true  },
      { GEN_TYPE_D,  0, GEN_TYPE_D,  0, GEN_TYPE_UB, 0, GEN_TYPE_UB,  0, 0, 0, true  },
      { GEN_TYPE_D,  0, GEN_TYPE_UD, 0, GEN_TYPE_B,  0, GEN_TYPE_B,   0, 0, 0, true  },

      { GEN_TYPE_F,  1, GEN_TYPE_F,  0, GEN_TYPE_HF, 0, GEN_TYPE_HF,  0, 0, 0, false },
      { GEN_TYPE_F,  0, GEN_TYPE_F,  1, GEN_TYPE_HF, 0, GEN_TYPE_HF,  0, 0, 0, false },
      { GEN_TYPE_F,  0, GEN_TYPE_F,  0, GEN_TYPE_HF, 1, GEN_TYPE_HF,  0, 0, 0, false },
      { GEN_TYPE_F,  0, GEN_TYPE_F,  0, GEN_TYPE_HF, 0, GEN_TYPE_HF,  1, 0, 0, false },
      { GEN_TYPE_F,  0, GEN_TYPE_F,  0, GEN_TYPE_HF, 0, GEN_TYPE_HF, 16, 0, 0, false },

      { GEN_TYPE_F,  devinfo.grf_size, GEN_TYPE_F, 0, GEN_TYPE_HF, 0, GEN_TYPE_HF, 0, 0, 0, false },
      { GEN_TYPE_F,  0, GEN_TYPE_F, devinfo.grf_size, GEN_TYPE_HF, 0, GEN_TYPE_HF, 0, 0, 0, false },
      { GEN_TYPE_F,  0, GEN_TYPE_F, 0, GEN_TYPE_HF, 0, GEN_TYPE_HF, devinfo.grf_size, 0, 0, false },

      { GEN_TYPE_UD, 0, GEN_TYPE_UD, 0, GEN_TYPE_UB, 0, GEN_TYPE_UB, 16, 0, 1, true  },
      { GEN_TYPE_UD, 0, GEN_TYPE_UD, 0, GEN_TYPE_UB, 0, GEN_TYPE_UB,  8, 0, 1, false },
      { GEN_TYPE_UD, 0, GEN_TYPE_UD, 0, GEN_TYPE_UB, 0, GEN_TYPE_UB, devinfo.grf_size, 0, 1, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_dpas(valid_exec_size);
      inst.dst.type = tests[i].dst_type;
      inst.dst.subnr = tests[i].dst_subnr;
      inst.src[0].type = tests[i].src0_type;
      inst.src[0].subnr = tests[i].src0_subnr;
      inst.src[1].type = tests[i].src1_type;
      inst.src[1].subnr = tests[i].src1_subnr;
      inst.src[2].type = tests[i].src2_type;
      inst.src[2].subnr = tests[i].src2_subnr;
      inst.dpas.src1_subbyte = tests[i].src1_subbyte;
      inst.dpas.src2_subbyte = tests[i].src2_subbyte;

      EXPECT_EQ(tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, cmp_requires_condition)
{
   for (gen_opcode op : { GEN_OP_CMP, GEN_OP_CMPN }) {
      SCOPED_TRACE(::testing::Message() << "opcode=" << unsigned(op));

      gen_inst inst = make_add(GEN_TYPE_D);
      inst.opcode = op;
      inst.cmod = GEN_CONDITION_NONE;
      EXPECT_FALSE(validate(inst));

      inst.cmod = GEN_CONDITION_ZE;
      EXPECT_TRUE(validate(inst));
   }
}

TEST_P(gen_validate_test, sel_requires_exactly_one_predicate_or_condition)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.opcode = GEN_OP_SEL;

   inst.cmod = GEN_CONDITION_NONE;
   inst.pred_control = GEN_PREDICATE_NONE;
   EXPECT_FALSE(validate(inst));

   inst.cmod = GEN_CONDITION_ZE;
   inst.pred_control = GEN_PREDICATE_NONE;
   EXPECT_TRUE(validate(inst));

   inst.cmod = GEN_CONDITION_NONE;
   inst.pred_control = GEN_PREDICATE_NORMAL;
   EXPECT_TRUE(validate(inst));

   inst.cmod = GEN_CONDITION_ZE;
   inst.pred_control = GEN_PREDICATE_NORMAL;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, csel_restrictions)
{
   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.opcode = GEN_OP_CSEL;
   inst.align16 = devinfo.ver == 9;
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.opcode = GEN_OP_CSEL;
   inst.align16 = devinfo.ver == 9;
   inst.cmod = GEN_CONDITION_NONE;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.opcode = GEN_OP_CSEL;
   inst.align16 = devinfo.ver == 9;
   inst.cmod = GEN_CONDITION_ZE;
   inst.pred_control = GEN_PREDICATE_NORMAL;
   EXPECT_FALSE(validate(inst));

   if (devinfo.ver == 9) {
      inst = make_mad(GEN_TYPE_F);
      inst.opcode = GEN_OP_CSEL;
      inst.align16 = true;
      inst.cmod = GEN_CONDITION_ZE;
      inst.dst.type = GEN_TYPE_D;
      inst.src[0].type = GEN_TYPE_D;
      inst.src[1].type = GEN_TYPE_D;
      inst.src[2].type = GEN_TYPE_D;
      EXPECT_FALSE(validate(inst));
   } else {
      inst = make_mad(GEN_TYPE_D);
      inst.opcode = GEN_OP_CSEL;
      inst.cmod = GEN_CONDITION_ZE;
      EXPECT_TRUE(validate(inst));

      inst = make_mad(GEN_TYPE_D);
      inst.opcode = GEN_OP_CSEL;
      inst.cmod = GEN_CONDITION_ZE;
      inst.src[2].type = GEN_TYPE_F;
      EXPECT_FALSE(validate(inst));

      inst = make_mad(GEN_TYPE_D);
      inst.opcode = GEN_OP_CSEL;
      inst.cmod = GEN_CONDITION_ZE;
      inst.src[2].type = GEN_TYPE_W;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, csel_src0_src1_type_restrictions)
{
   if (devinfo.ver == 9)
      GTEST_SKIP();

   gen_inst inst = make_mad(GEN_TYPE_D);
   inst.opcode = GEN_OP_CSEL;
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_D);
   inst.opcode = GEN_OP_CSEL;
   inst.cmod = GEN_CONDITION_ZE;
   inst.src[0].type = GEN_TYPE_W;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_D);
   inst.opcode = GEN_OP_CSEL;
   inst.cmod = GEN_CONDITION_ZE;
   inst.src[1].type = GEN_TYPE_F;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, csel_destination_type_legality)
{
   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.opcode = GEN_OP_CSEL;
   inst.align16 = devinfo.ver == 9;
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_B);
   inst.opcode = GEN_OP_CSEL;
   inst.align16 = devinfo.ver == 9;
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_HF);
   inst.opcode = GEN_OP_CSEL;
   inst.align16 = devinfo.ver == 9;
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_EQ(devinfo.ver != 9, validate(inst));
}

TEST_P(gen_validate_test, bfn_restrictions)
{
   if (devinfo.ver < 12)
      return;

   gen_inst inst = make_mad(GEN_TYPE_UD);
   inst.opcode = GEN_OP_BFN;
   EXPECT_TRUE(validate(inst));

   for (gen_condition cmod : { GEN_CONDITION_NONE, GEN_CONDITION_ZE,
                               GEN_CONDITION_GT, GEN_CONDITION_LT }) {
      SCOPED_TRACE(::testing::Message() << "cmod=" << unsigned(cmod));
      inst = make_mad(GEN_TYPE_UD);
      inst.opcode = GEN_OP_BFN;
      inst.cmod = cmod;
      EXPECT_TRUE(validate(inst));
   }

   inst = make_mad(GEN_TYPE_UD);
   inst.opcode = GEN_OP_BFN;
   inst.cmod = GEN_CONDITION_NZ;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_UD);
   inst.opcode = GEN_OP_BFN;
   inst.saturate = true;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_UD);
   inst.opcode = GEN_OP_BFN;
   inst.src[1].type = GEN_TYPE_D;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_UD);
   inst.opcode = GEN_OP_BFN;
   inst.src[2].negate = true;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, avg_restrictions)
{
   gen_inst inst = make_add(GEN_TYPE_UD);
   inst.opcode = GEN_OP_AVG;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_F);
   inst.opcode = GEN_OP_AVG;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_UQ);
   inst.opcode = GEN_OP_AVG;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, line_pln_src0_must_be_scalar)
{
   if (devinfo.ver != 9)
      return;

   for (gen_opcode op : { GEN_OP_LINE, GEN_OP_PLN }) {
      SCOPED_TRACE(::testing::Message() << "opcode=" << unsigned(op));

      gen_inst inst = make_add(GEN_TYPE_F);
      inst.opcode = op;
      inst.src[0].region.vstride = 0;
      inst.src[0].region.width = 1;
      inst.src[0].region.hstride = 0;
      EXPECT_TRUE(validate(inst));

      inst = make_add(GEN_TYPE_F);
      inst.opcode = op;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, ror_rol_restrictions)
{
   if (devinfo.ver < 11)
      return;

   for (gen_opcode op : { GEN_OP_ROR, GEN_OP_ROL }) {
      SCOPED_TRACE(::testing::Message() << "opcode=" << unsigned(op));

      gen_inst inst = make_add(GEN_TYPE_UD);
      inst.opcode = op;
      EXPECT_TRUE(validate(inst));

      inst = make_add(GEN_TYPE_UD);
      inst.opcode = op;
      inst.dst.type = GEN_TYPE_F;
      EXPECT_FALSE(validate(inst));

      inst = make_add(GEN_TYPE_UD);
      inst.opcode = op;
      inst.src[0].type = GEN_TYPE_UW;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, lrp_requires_float_types)
{
   if (devinfo.ver != 9)
      return;

   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.opcode = GEN_OP_LRP;
   inst.align16 = true;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.opcode = GEN_OP_LRP;
   inst.align16 = true;
   inst.src[2].type = GEN_TYPE_D;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, math_restrictions)
{
   gen_inst inst = make_math(GEN_MATH_INT_DIV_QUOTIENT, GEN_TYPE_D);
   inst.src[0].type = GEN_TYPE_D;
   inst.src[1].type = GEN_TYPE_D;
   inst.dst.type = GEN_TYPE_D;
   EXPECT_EQ(devinfo.verx10 < 125, validate(inst));

   inst = make_math(GEN_MATH_INT_DIV_QUOTIENT, GEN_TYPE_D);
   inst.src[0].type = GEN_TYPE_D;
   inst.src[1].type = GEN_TYPE_D;
   inst.dst.type = GEN_TYPE_D;
   inst.src[1].negate = true;
   EXPECT_FALSE(validate(inst));

   inst = make_math(GEN_MATH_SQRT, GEN_TYPE_F);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_NULL;
   EXPECT_FALSE(validate(inst));

   inst = make_math(GEN_MATH_POW, GEN_TYPE_F);
   EXPECT_EQ(devinfo.verx10 < 125, validate(inst));

   inst = make_math(GEN_MATH_SQRT, GEN_TYPE_F);
   EXPECT_TRUE(validate(inst));

   if (devinfo.has_64bit_float && devinfo.verx10 >= 125) {
      inst = make_math(GEN_MATH_SQRT, GEN_TYPE_DF);
      EXPECT_FALSE(validate(inst));
   }

   inst = make_math(GEN_MATH_INVM, GEN_TYPE_F);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_EQ(devinfo.ver < 20, validate(inst));

   inst = make_math(GEN_MATH_SQRT, GEN_TYPE_F);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, math_two_source_type_rules)
{
   gen_inst inst = make_math(GEN_MATH_INVM, GEN_TYPE_F);
   inst.src[1].type = GEN_TYPE_HF;
   inst.src[1].region.vstride = 16;
   inst.src[1].region.width = 8;
   inst.src[1].region.hstride = 2;
   EXPECT_EQ(devinfo.verx10 < 125, validate(inst));

   inst = make_math(GEN_MATH_INVM, GEN_TYPE_F);
   inst.src[1].type = GEN_TYPE_UD;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, math_two_source_type_rules_pow_and_fdiv)
{
   for (gen_math func : { GEN_MATH_POW, GEN_MATH_FDIV }) {
      SCOPED_TRACE(::testing::Message() << "func=" << unsigned(func));

      gen_inst inst = make_math(func, GEN_TYPE_F);
      inst.src[1].type = GEN_TYPE_HF;
      inst.src[1].region.vstride = 16;
      inst.src[1].region.width = 8;
      inst.src[1].region.hstride = 2;
      EXPECT_EQ(devinfo.verx10 < 125, validate(inst));

      inst = make_math(func, GEN_TYPE_F);
      inst.src[1].type = GEN_TYPE_UD;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, math_one_source_type_match_on_gfx125)
{
   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      unsigned dst_stride;
      unsigned src0_stride;
      bool pre_gfx125_expected;
   } tests[] = {
      { GEN_TYPE_F,  GEN_TYPE_F,  1, 1, true  },
      { GEN_TYPE_HF, GEN_TYPE_HF, 2, 2, true  },
      { GEN_TYPE_HF, GEN_TYPE_F,  2, 1, true  },
      { GEN_TYPE_F,  GEN_TYPE_HF, 1, 2, true  },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

      gen_inst inst = make_math(GEN_MATH_SQRT, tests[i].dst_type);
      inst.dst.type = tests[i].dst_type;
      inst.dst.region.hstride = tests[i].dst_stride;
      inst.src[0].type = tests[i].src0_type;
      inst.src[0].region.width = 4;
      inst.src[0].region.hstride = tests[i].src0_stride;
      inst.src[0].region.vstride = inst.src[0].region.width * tests[i].src0_stride;
      inst.src[1].file = GEN_ARF;
      inst.src[1].nr = GEN_ARF_NULL;

      const bool expected = devinfo.verx10 >= 125 ?
         (tests[i].dst_type == tests[i].src0_type) :
         tests[i].pre_gfx125_expected;
      EXPECT_EQ(expected, validate(inst));
   }
}

TEST_P(gen_validate_test, math_ieee_macro_df_type_rules)
{
   if (!devinfo.has_64bit_float)
      GTEST_SKIP();

   gen_inst inst = make_math(GEN_MATH_RSQRTM, GEN_TYPE_DF);
   inst.exec_size = 4;
   inst.dst.type = GEN_TYPE_DF;
   inst.src[0].type = GEN_TYPE_DF;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_NULL;
   EXPECT_EQ(devinfo.verx10 >= 125, validate(inst));

   inst = make_math(GEN_MATH_INVM, GEN_TYPE_DF);
   inst.exec_size = 4;
   inst.dst.type = GEN_TYPE_DF;
   inst.src[0].type = GEN_TYPE_DF;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;
   inst.src[1].type = GEN_TYPE_DF;
   inst.src[1].region.vstride = 4;
   inst.src[1].region.width = 4;
   inst.src[1].region.hstride = 1;
   EXPECT_EQ(devinfo.verx10 >= 125, validate(inst));

   inst = make_math(GEN_MATH_INVM, GEN_TYPE_DF);
   inst.exec_size = 4;
   inst.dst.type = GEN_TYPE_DF;
   inst.src[0].type = GEN_TYPE_DF;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;
   inst.src[1].type = GEN_TYPE_F;
   inst.src[1].region.vstride = 4;
   inst.src[1].region.width = 4;
   inst.src[1].region.hstride = 1;
   EXPECT_FALSE(validate(inst));

   inst = make_math(GEN_MATH_RSQRTM, GEN_TYPE_F);
   inst.exec_size = 4;
   inst.dst.type = GEN_TYPE_F;
   inst.src[0].type = GEN_TYPE_DF;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_NULL;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, math_ieee_macro_accumulator_rules)
{
   gen_inst inst = make_math(GEN_MATH_RSQRTM, GEN_TYPE_F);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_EQ(devinfo.ver < 20, validate(inst));

   inst = make_math(GEN_MATH_INVM, GEN_TYPE_F);
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_EQ(devinfo.ver < 20, validate(inst));
}

TEST_P(gen_validate_test, math_int_div_unsigned_and_type_mismatch_rules)
{
   gen_inst inst = make_math(GEN_MATH_INT_DIV_REMAINDER, GEN_TYPE_UD);
   inst.dst.type = GEN_TYPE_UD;
   inst.src[0].type = GEN_TYPE_UD;
   inst.src[1].type = GEN_TYPE_UD;
   EXPECT_EQ(devinfo.verx10 < 125, validate(inst));

   inst = make_math(GEN_MATH_INT_DIV_REMAINDER, GEN_TYPE_UD);
   inst.dst.type = GEN_TYPE_UD;
   inst.src[0].type = GEN_TYPE_UD;
   inst.src[1].type = GEN_TYPE_D;
   EXPECT_FALSE(validate(inst));

   inst = make_math(GEN_MATH_INT_DIV_REMAINDER, GEN_TYPE_UD);
   inst.dst.type = GEN_TYPE_D;
   inst.src[0].type = GEN_TYPE_UD;
   inst.src[1].type = GEN_TYPE_UD;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, logic_op_src1_accumulator_modifier_restrictions)
{
   gen_inst inst = make_add(GEN_TYPE_UD);
   inst.opcode = GEN_OP_XOR;
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_ACCUMULATOR;
   inst.src[1].negate = true;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, logic_op_restrictions)
{
   gen_inst inst = make_add(GEN_TYPE_UD);
   inst.opcode = GEN_OP_AND;
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_UD);
   inst.opcode = GEN_OP_AND;
   inst.src[0].abs = true;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_UD);
   inst.opcode = GEN_OP_AND;
   inst.src[1].abs = true;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_UD);
   inst.opcode = GEN_OP_XOR;
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   inst.src[0].negate = true;
   EXPECT_FALSE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD);
   inst.opcode = GEN_OP_NOT;
   inst.cmod = GEN_CONDITION_OV;
   EXPECT_FALSE(validate(inst));

   inst = make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD);
   inst.opcode = GEN_OP_NOT;
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, logic_op_immediate_src1_abs_is_allowed)
{
   gen_inst inst = make_add(GEN_TYPE_UD);
   inst.opcode = GEN_OP_AND;
   inst.src[1].file = GEN_IMM;
   inst.src[1].type = GEN_TYPE_UD;
   inst.src[1].imm = 1;
   inst.src[1].abs = true;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, logic_op_condition_modifier_corners)
{
   for (gen_opcode op : { GEN_OP_AND, GEN_OP_OR, GEN_OP_XOR, GEN_OP_NOT }) {
      SCOPED_TRACE(::testing::Message() << "opcode=" << unsigned(op));

      gen_inst inst = op == GEN_OP_NOT ?
         make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD) : make_add(GEN_TYPE_UD);
      inst.opcode = op;
      inst.cmod = GEN_CONDITION_ZE;
      EXPECT_TRUE(validate(inst));

      inst = op == GEN_OP_NOT ?
         make_rr_mov(GEN_TYPE_UD, GEN_TYPE_UD) : make_add(GEN_TYPE_UD);
      inst.opcode = op;
      inst.cmod = GEN_CONDITION_UN;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, bfi2_restrictions)
{
   gen_inst inst = make_mad(GEN_TYPE_D);
   inst.opcode = GEN_OP_BFI2;
   if (devinfo.ver == 9)
      inst.align16 = true;
   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_D);
   inst.opcode = GEN_OP_BFI2;
   if (devinfo.ver == 9)
      inst.align16 = true;
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_D);
   inst.opcode = GEN_OP_BFI2;
   if (devinfo.ver == 9)
      inst.align16 = true;
   inst.saturate = true;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_F);
   inst.opcode = GEN_OP_BFI2;
   if (devinfo.ver == 9)
      inst.align16 = true;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_D);
   inst.opcode = GEN_OP_BFI2;
   if (devinfo.ver == 9)
      inst.align16 = true;
   inst.src[2].type = GEN_TYPE_UD;
   EXPECT_FALSE(validate(inst));

   for (unsigned i = 0; i < 3; i++) {
      SCOPED_TRACE(::testing::Message() << "source=" << i);

      inst = make_mad(GEN_TYPE_D);
      inst.opcode = GEN_OP_BFI2;
      if (devinfo.ver == 9)
         inst.align16 = true;
      inst.src[i].file = GEN_IMM;
      inst.src[i].type = GEN_TYPE_D;
      inst.src[i].imm = 0;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, mul_additional_restrictions)
{
   gen_inst inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UW, GEN_TYPE_UD);
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UW);
   inst.saturate = true;
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UW);
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UW);
   inst.src[1].negate = true;
   EXPECT_EQ(devinfo.ver < 12, validate(inst));
}

TEST_P(gen_validate_test, mul_dword_source_modifier_corners)
{
   gen_inst inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UW);
   inst.src[0].negate = true;
   EXPECT_TRUE(validate(inst));

   inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UW);
   inst.src[1].abs = true;
   EXPECT_EQ(devinfo.ver < 12, validate(inst));

   inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UW, GEN_TYPE_UD);
   inst.src[0].negate = true;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, mul_immediate_dword_source_order_rule)
{
   gen_inst inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UW, GEN_TYPE_UD);
   inst.src[1].file = GEN_IMM;
   inst.src[1].imm = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UW);
   inst.src[1].file = GEN_IMM;
   inst.src[1].imm = 0;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, mul_dont_mix_floats_and_ints)
{
   gen_inst inst = make_mul(GEN_TYPE_F, GEN_TYPE_UD, GEN_TYPE_UD);
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_F, GEN_TYPE_F, GEN_TYPE_UD);
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_F, GEN_TYPE_UD, GEN_TYPE_VF);
   inst.src[1].file = GEN_IMM;
   inst.src[1].imm = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_F, GEN_TYPE_F, GEN_TYPE_F);
   EXPECT_TRUE(validate(inst));

   inst = make_qword_rule_mul();
   inst.dst.type = GEN_TYPE_UD;
   inst.src[0].type = GEN_TYPE_UD;
   inst.src[1].type = GEN_TYPE_UD;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, mul_dont_accept_int_accumulator_src)
{
   gen_inst inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UD);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UD);
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_FALSE(validate(inst));

   inst = make_mul(GEN_TYPE_F, GEN_TYPE_F, GEN_TYPE_F);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_ACCUMULATOR;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, add_dont_mix_integer_and_float_sources)
{
   gen_inst inst = make_add(GEN_TYPE_F);
   inst.src[1].type = GEN_TYPE_UD;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_F);
   inst.src[0].type = GEN_TYPE_UD;
   EXPECT_FALSE(validate(inst));

   inst = make_add(GEN_TYPE_F);
   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_UD);
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, vector_immediate_destination_alignment)
{
   const struct {
      gen_reg_type dst_type;
      gen_reg_type src_type;
      unsigned subnr;
      unsigned exec_size;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F, GEN_TYPE_VF,  0, 4, true  },
      { GEN_TYPE_F, GEN_TYPE_VF, 16, 4, true  },
      { GEN_TYPE_F, GEN_TYPE_VF,  1, 4, false },

      { GEN_TYPE_W, GEN_TYPE_V,   0, 8, true  },
      { GEN_TYPE_W, GEN_TYPE_V,  16, 8, true  },
      { GEN_TYPE_W, GEN_TYPE_V,   1, 8, false },

      { GEN_TYPE_W, GEN_TYPE_UV,  0, 8, true  },
      { GEN_TYPE_W, GEN_TYPE_UV, 16, 8, true  },
      { GEN_TYPE_W, GEN_TYPE_UV,  1, 8, false },
   };

   for (const auto &test : tests) {
      SCOPED_TRACE(::testing::Message()
                   << "dst_type=" << unsigned(test.dst_type)
                   << " src_type=" << unsigned(test.src_type)
                   << " subnr=" << test.subnr
                   << " exec_size=" << test.exec_size);

      gen_inst inst = make_mov(test.dst_type, test.src_type,
                               test.exec_size, test.subnr, 1);
      EXPECT_EQ(test.expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, vector_immediate_destination_stride)
{
   const struct {
      gen_reg_type dst_type;
      gen_reg_type src_type;
      unsigned stride;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F, GEN_TYPE_VF, 1, true  },
      { GEN_TYPE_F, GEN_TYPE_VF, 2, false },
      { GEN_TYPE_D, GEN_TYPE_VF, 1, true  },
      { GEN_TYPE_D, GEN_TYPE_VF, 2, false },
      { GEN_TYPE_W, GEN_TYPE_VF, 2, true  },
      { GEN_TYPE_B, GEN_TYPE_VF, 4, true  },

      { GEN_TYPE_W, GEN_TYPE_V,  1, true  },
      { GEN_TYPE_W, GEN_TYPE_V,  2, false },
      { GEN_TYPE_W, GEN_TYPE_V,  4, false },
      { GEN_TYPE_B, GEN_TYPE_V,  2, true  },

      { GEN_TYPE_W, GEN_TYPE_UV, 1, true  },
      { GEN_TYPE_W, GEN_TYPE_UV, 2, false },
      { GEN_TYPE_W, GEN_TYPE_UV, 4, false },
      { GEN_TYPE_B, GEN_TYPE_UV, 2, true  },
   };

   for (const auto &test : tests) {
      SCOPED_TRACE(::testing::Message()
                   << "dst_type=" << unsigned(test.dst_type)
                   << " src_type=" << unsigned(test.src_type)
                   << " stride=" << test.stride);

      gen_inst inst = make_mov(test.dst_type, test.src_type, 8, 0, test.stride);
      EXPECT_EQ(test.expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, qword_low_power_align1_regioning_restrictions)
{
   const bool is_9lp = intel_device_info_is_9lp(&devinfo);
   const bool reject_region = is_9lp || devinfo.verx10 >= 125;

   gen_inst inst = make_qword_rule_mul();
   EXPECT_TRUE(validate(inst));

   inst = make_qword_rule_mul();
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;
   EXPECT_EQ(!reject_region, validate(inst));

   inst = make_qword_rule_mul();
   inst.src[0].region.vstride = 0;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 2;
   EXPECT_EQ(!reject_region, validate(inst));

   inst = make_qword_rule_mul();
   inst.dst.subnr = 4;
   EXPECT_EQ(!reject_region, validate(inst));

   if (devinfo.has_64bit_float || devinfo.has_64bit_int) {
      const gen_reg_type type = devinfo.has_64bit_float ? GEN_TYPE_DF : GEN_TYPE_Q;

      inst = make_rr_mov(type, type, 2);
      inst.dst.region.hstride = 1;
      inst.dst.subnr = 8;
      inst.src[0].region.vstride = 0;
      inst.src[0].region.width = 1;
      inst.src[0].region.hstride = 0;
      EXPECT_TRUE(validate(inst));
   }
}

TEST_P(gen_validate_test, qword_low_power_no_indirect_addressing)
{
   const bool is_9lp = intel_device_info_is_9lp(&devinfo);

   gen_inst inst = make_rr_mov(GEN_TYPE_F, GEN_TYPE_F, 4);
   inst.dst.indirect = true;
   EXPECT_TRUE(validate(inst));

   inst = make_qword_rule_mul();
   inst.dst.indirect = true;
   EXPECT_EQ(!is_9lp, validate(inst));

   inst = make_qword_rule_mul();
   inst.src[0].indirect = true;
   EXPECT_EQ(!is_9lp, validate(inst));
}

TEST_P(gen_validate_test, qword_low_power_no_64bit_arf)
{
   const bool is_9lp = intel_device_info_is_9lp(&devinfo);

   gen_inst inst = make_qword_rule_mul();
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_ACCUMULATOR;
   EXPECT_EQ(!is_9lp, validate(inst));

   inst = make_qword_rule_mul();
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_NULL;
   EXPECT_TRUE(validate(inst));

   inst = make_qword_rule_mul();
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_NULL;
   inst.acc_wr_control = true;
   EXPECT_EQ(!is_9lp && devinfo.ver < 20, validate(inst));

   if (devinfo.has_64bit_float) {
      inst = {};
      inst.opcode = GEN_OP_MAC;
      inst.exec_size = 4;

      inst.dst.file = GEN_GRF;
      inst.dst.type = GEN_TYPE_DF;
      inst.dst.nr = 0;
      inst.dst.region.hstride = 1;

      inst.src[0].file = GEN_GRF;
      inst.src[0].type = GEN_TYPE_DF;
      inst.src[0].nr = 1;
      inst.src[0].region.vstride = 4;
      inst.src[0].region.width = 4;
      inst.src[0].region.hstride = 1;

      inst.src[1].file = GEN_GRF;
      inst.src[1].type = GEN_TYPE_DF;
      inst.src[1].nr = 2;
      inst.src[1].region.vstride = 4;
      inst.src[1].region.width = 4;
      inst.src[1].region.hstride = 1;

      EXPECT_EQ(!is_9lp, validate(inst));
   }
}

TEST_P(gen_validate_test, align16_64_bit_integer)
{
   if (devinfo.ver >= 11)
      return;

   if (!devinfo.has_64bit_float && !devinfo.has_64bit_int)
      return;

   const gen_reg_type dst_type = devinfo.has_64bit_int ? GEN_TYPE_Q : GEN_TYPE_DF;
   const gen_reg_type src_type = devinfo.has_64bit_int ? GEN_TYPE_D : GEN_TYPE_F;

   gen_inst inst = make_rr_mov(dst_type, src_type, 2);
   inst.align16 = true;
   inst.dst.region.hstride = 1;
   inst.src[0].region.vstride = 2;
   inst.src[0].region.width = 2;
   inst.src[0].region.hstride = 1;
   EXPECT_TRUE(validate(inst));

   inst = make_rr_mov(dst_type, src_type, 4);
   inst.align16 = true;
   inst.dst.region.hstride = 1;
   inst.src[0].region.vstride = 4;
   inst.src[0].region.width = 4;
   inst.src[0].region.hstride = 1;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, qword_low_power_no_depctrl)
{
   if (devinfo.ver >= 12)
      return;

   const bool is_9lp = intel_device_info_is_9lp(&devinfo);

   gen_inst inst = make_qword_rule_mul();
   inst.no_dd_check = true;
   EXPECT_EQ(!is_9lp, validate(inst));

   inst = make_qword_rule_mul();
   inst.no_dd_clear = true;
   EXPECT_EQ(!is_9lp, validate(inst));
}

TEST_P(gen_validate_test, fusion_control_only_allowed_on_gfx12_send)
{
   gen_inst inst = make_add(GEN_TYPE_D);
   inst.fusion_control = true;
   EXPECT_FALSE(validate(inst));

   gen_inst send = make_send(GEN_OP_SEND);
   send.fusion_control = true;
   EXPECT_EQ(devinfo.ver == 12, validate(send));
}

TEST_P(gen_validate_test, acc_wr_control_not_present_on_gfx20plus)
{
   gen_inst inst = make_rr_mov(GEN_TYPE_D, GEN_TYPE_D, 4);
   inst.acc_wr_control = true;

   EXPECT_EQ(devinfo.ver < 20, validate(inst));
}

TEST_P(gen_validate_test, send_file_restrictions)
{
   if (devinfo.ver < 12) {
      gen_inst inst = make_send(GEN_OP_SEND);
      inst.src[0].file = GEN_ARF;
      inst.src[0].nr = GEN_ARF_FLAG;
      EXPECT_FALSE(validate(inst));
   } else {
      gen_inst inst = make_send(GEN_OP_SEND);
      inst.src[1].file = GEN_ARF;
      inst.src[1].nr = GEN_ARF_FLAG;
      EXPECT_FALSE(validate(inst));
   }
}

TEST_P(gen_validate_test, send_eot_payload_restrictions)
{
   if (devinfo.ver < 12) {
      gen_inst inst = make_send(GEN_OP_SEND);
      inst.send.eot = true;
      inst.src[0].nr = 111;
      EXPECT_FALSE(validate(inst));

      inst.src[0].nr = 112;
      EXPECT_TRUE(validate(inst));
      return;
   }

   gen_inst inst = make_send(GEN_OP_SEND);
   inst.send.eot = true;
   inst.src[0].nr = 111;
   inst.src[1].file = GEN_GRF;
   inst.src[1].nr = 112;
   EXPECT_EQ(devinfo.ver >= 20, validate(inst));

   inst = make_send(GEN_OP_SEND);
   inst.send.eot = true;
   inst.src[0].nr = 112;
   inst.src[1].file = GEN_GRF;
   inst.src[1].nr = 111;
   EXPECT_EQ(devinfo.ver >= 20, validate(inst));

   inst = make_send(GEN_OP_SEND);
   inst.send.eot = true;
   inst.src[0].nr = 112;
   inst.src[1].file = GEN_GRF;
   inst.src[1].nr = 112;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, send_direct_addressing_restrictions)
{
   gen_inst inst = make_send(devinfo.ver >= 12 ? GEN_OP_SEND : GEN_OP_SENDS);
   inst.src[0].indirect = true;
   EXPECT_FALSE(validate(inst));

   inst = make_send(devinfo.ver >= 12 ? GEN_OP_SEND : GEN_OP_SENDS);
   inst.src[1].file = GEN_GRF;
   inst.src[1].nr = 2;
   inst.src[1].indirect = true;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, gen9_send_return_address_overlap)
{
   if (devinfo.ver != 9)
      GTEST_SKIP();

   gen_inst inst = make_send(GEN_OP_SEND);
   inst.dst.file = GEN_GRF;
   inst.dst.nr = 127;
   inst.send.desc_imm = make_message_desc(2, 1, false);
   inst.src[0].nr = 126;
   EXPECT_FALSE(validate(inst));

   inst = make_send(GEN_OP_SEND);
   inst.dst.file = GEN_GRF;
   inst.dst.nr = 127;
   inst.send.desc_imm = make_message_desc(2, 1, false);
   inst.src[0].nr = 125;
   EXPECT_TRUE(validate(inst));

   inst = make_send(GEN_OP_SEND);
   inst.send.desc_imm = make_message_desc(2, 1, false);
   inst.src[0].nr = 126;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, split_send_payload_overlap)
{
   gen_inst inst = make_send(devinfo.ver >= 12 ? GEN_OP_SEND : GEN_OP_SENDS);
   inst.src[0].nr = 4;
   inst.src[1].file = GEN_GRF;
   inst.src[1].nr = 6;
   inst.send.desc_imm = make_message_desc(2, 1, false);
   inst.send.src1_len = 1;
   EXPECT_TRUE(validate(inst));

   inst = make_send(devinfo.ver >= 12 ? GEN_OP_SEND : GEN_OP_SENDS);
   inst.src[0].nr = 4;
   inst.src[1].file = GEN_GRF;
   inst.src[1].nr = 5;
   inst.send.desc_imm = make_message_desc(2, 1, false);
   inst.send.src1_len = 1;
   EXPECT_FALSE(validate(inst));

   inst = make_send(devinfo.ver >= 12 ? GEN_OP_SEND : GEN_OP_SENDS);
   inst.src[0].nr = 4;
   inst.src[1].file = GEN_GRF;
   inst.src[1].nr = 4;
   inst.send.desc_is_reg = true;
   inst.send.ex_desc_is_reg = true;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, split_send_payload_overlap_one_sided_descriptor_fallback)
{
   gen_inst inst = make_send(devinfo.ver >= 12 ? GEN_OP_SEND : GEN_OP_SENDS);
   inst.src[0].nr = 4;
   inst.src[1].file = GEN_GRF;
   inst.src[1].nr = 7;
   inst.send.desc_imm = make_message_desc(3, 1, false);
   inst.send.ex_desc_is_reg = true;
   EXPECT_TRUE(validate(inst));

   inst.src[1].nr = 6;
   EXPECT_FALSE(validate(inst));

   inst = make_send(devinfo.ver >= 12 ? GEN_OP_SEND : GEN_OP_SENDS);
   inst.src[0].nr = 5;
   inst.src[1].file = GEN_GRF;
   inst.src[1].nr = 4;
   inst.send.desc_is_reg = true;
   inst.send.src1_len = 2;
   EXPECT_FALSE(validate(inst));

   inst.src[0].nr = 6;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, send_descriptor_lsc_requires_platform_support)
{
   for (gen_sfid sfid : { GEN_SFID_UGM, GEN_SFID_SLM, GEN_SFID_TGM }) {
      SCOPED_TRACE(::testing::Message() << "sfid=" << unsigned(sfid));

      gen_inst inst = make_send(GEN_OP_SEND);
      inst.send.sfid = sfid;
      if (devinfo.has_lsc) {
         inst.send.desc_imm =
            make_message_desc(1, 1, false) |
            lsc_msg_desc(&devinfo,
                         LSC_OP_LOAD,
                         LSC_ADDR_SURFTYPE_FLAT,
                         LSC_ADDR_SIZE_A32,
                         LSC_DATA_SIZE_D32,
                         1,
                         false,
                         0);
      } else {
         inst.send.desc_imm = make_message_desc(1, 1, false);
      }

      EXPECT_EQ(devinfo.has_lsc, validate(inst));
   }
}

TEST_P(gen_validate_test, send_descriptor_lsc_transpose_requires_exec_size_1)
{
   if (!devinfo.has_lsc)
      GTEST_SKIP();

   gen_inst inst = make_send(GEN_OP_SEND);
   inst.send.sfid = GEN_SFID_UGM;
   inst.send.desc_imm =
      make_message_desc(1, 1, false) |
      lsc_msg_desc(&devinfo,
                   LSC_OP_LOAD,
                   LSC_ADDR_SURFTYPE_FLAT,
                   LSC_ADDR_SIZE_A32,
                   LSC_DATA_SIZE_D32,
                   2,
                   true,
                   0);

   inst.exec_size = 8;
   EXPECT_FALSE(validate(inst));

   inst.exec_size = 1;
   EXPECT_TRUE(validate(inst));
}
TEST_P(gen_validate_test, send_descriptor_lsc_transpose_requires_exec_size_1_across_sfids)
{
   if (!devinfo.has_lsc)
      GTEST_SKIP();

   for (gen_sfid sfid : { GEN_SFID_UGM, GEN_SFID_SLM, GEN_SFID_TGM }) {
      for (lsc_opcode op : { LSC_OP_LOAD, LSC_OP_STORE }) {
         SCOPED_TRACE(::testing::Message()
                      << "sfid=" << unsigned(sfid)
                      << " op=" << unsigned(op));

         gen_inst inst = make_send(GEN_OP_SEND);
         inst.send.sfid = sfid;
         inst.send.desc_imm =
            make_message_desc(1, 1, false) |
            lsc_msg_desc(&devinfo,
                         op,
                         LSC_ADDR_SURFTYPE_FLAT,
                         LSC_ADDR_SIZE_A32,
                         LSC_DATA_SIZE_D32,
                         2,
                         true,
                         0);

         inst.exec_size = 8;
         EXPECT_FALSE(validate(inst));

         inst.exec_size = 1;
         EXPECT_TRUE(validate(inst));
      }
   }
}

TEST_P(gen_validate_test, send_descriptor_pre_gfx20_urb_requires_header)
{
   if (devinfo.ver >= 20)
      GTEST_SKIP();

   gen_inst inst = make_send(GEN_OP_SEND);
   inst.send.sfid = GEN_SFID_URB;
   inst.send.desc_imm = make_message_desc(1, 1, false) | 4;
   EXPECT_FALSE(validate(inst));

   inst.send.desc_imm = make_message_desc(1, 1, true) | 4;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, send_descriptor_pre_gfx20_urb_simd8_read_requires_response)
{
   if (devinfo.ver >= 20)
      GTEST_SKIP();

   gen_inst inst = make_send(GEN_OP_SEND);
   inst.send.sfid = GEN_SFID_URB;
   inst.send.desc_imm = make_message_desc(1, 0, true) | 8;
   EXPECT_FALSE(validate(inst));

   inst.send.desc_imm = make_message_desc(1, 1, true) | 8;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, send_descriptor_pre_gfx20_urb_invalid_message)
{
   if (devinfo.ver >= 20)
      GTEST_SKIP();

   gen_inst inst = make_send(GEN_OP_SEND);
   inst.send.sfid = GEN_SFID_URB;
   inst.send.desc_imm = make_message_desc(1, 1, true) | 0;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, send_descriptor_pre_gfx20_urb_fence_requires_gfx125)
{
   if (devinfo.ver >= 20)
      GTEST_SKIP();

   gen_inst inst = make_send(GEN_OP_SEND);
   inst.send.sfid = GEN_SFID_URB;
   inst.send.desc_imm = make_message_desc(1, 1, true) | 9;
   EXPECT_EQ(devinfo.verx10 >= 125, validate(inst));
}

TEST_P(gen_validate_test, send_descriptor_register_descriptor_skips_immediate_validation)
{
   gen_inst inst = make_send(GEN_OP_SEND);

   if (devinfo.ver < 20) {
      inst.send.sfid = GEN_SFID_URB;
      inst.send.desc_imm = make_message_desc(1, 0, false) | 8;
      EXPECT_FALSE(validate(inst));

      inst.send.desc_is_reg = true;
      EXPECT_TRUE(validate(inst));
      return;
   }

   if (!devinfo.has_lsc)
      GTEST_SKIP();

   inst.send.sfid = GEN_SFID_UGM;
   inst.send.desc_imm =
      make_message_desc(1, 1, false) |
      lsc_msg_desc(&devinfo,
                   LSC_OP_LOAD,
                   LSC_ADDR_SURFTYPE_FLAT,
                   LSC_ADDR_SIZE_A32,
                   LSC_DATA_SIZE_D32,
                   2,
                   true,
                   0);
   inst.exec_size = 8;
   EXPECT_FALSE(validate(inst));

   inst.send.desc_is_reg = true;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, send_descriptor_split_send_register_combinations)
{
   if (devinfo.ver < 12)
      GTEST_SKIP();

   gen_inst inst = make_send(GEN_OP_SEND);

   if (devinfo.ver < 20) {
      inst.send.sfid = GEN_SFID_URB;
      inst.send.desc_imm = make_message_desc(1, 0, false) | 8;
      EXPECT_FALSE(validate(inst));

      inst.send.ex_desc_is_reg = true;
      EXPECT_FALSE(validate(inst));

      inst.send.desc_is_reg = true;
      EXPECT_TRUE(validate(inst));

      inst.send.ex_desc_is_reg = false;
      EXPECT_TRUE(validate(inst));
      return;
   }

   if (!devinfo.has_lsc)
      GTEST_SKIP();

   inst.send.sfid = GEN_SFID_UGM;
   inst.send.desc_imm =
      make_message_desc(1, 1, false) |
      lsc_msg_desc(&devinfo,
                   LSC_OP_LOAD,
                   LSC_ADDR_SURFTYPE_FLAT,
                   LSC_ADDR_SIZE_A32,
                   LSC_DATA_SIZE_D32,
                   2,
                   true,
                   0);
   inst.exec_size = 8;
   EXPECT_FALSE(validate(inst));

   inst.send.ex_desc_is_reg = true;
   EXPECT_FALSE(validate(inst));

   inst.send.desc_is_reg = true;
   EXPECT_TRUE(validate(inst));

   inst.send.ex_desc_is_reg = false;
   EXPECT_TRUE(validate(inst));
}

TEST_P(gen_validate_test, xe2_register_region_special_restrictions_for_3src_src0_and_src1)
{
   if (devinfo.verx10 < 200)
      GTEST_SKIP();

   gen_inst inst = make_mad(GEN_TYPE_W);
   inst.dst.type = GEN_TYPE_W;
   inst.dst.subnr = 0;
   inst.dst.region.hstride = 1;

   for (unsigned i = 0; i < 3; i++) {
      inst.src[i].type = GEN_TYPE_W;
      inst.src[i].region.vstride = 2;
      inst.src[i].region.width = 1;
      inst.src[i].region.hstride = 0;
      inst.src[i].subnr = 0;
   }

   EXPECT_TRUE(validate(inst));

   inst = make_mad(GEN_TYPE_W);
   inst.dst.type = GEN_TYPE_W;
   inst.dst.subnr = 0;
   inst.dst.region.hstride = 1;
   for (unsigned i = 0; i < 3; i++) {
      inst.src[i].type = GEN_TYPE_W;
      inst.src[i].region.vstride = 2;
      inst.src[i].region.width = 1;
      inst.src[i].region.hstride = 0;
      inst.src[i].subnr = 0;
   }
   inst.src[0].subnr = 4;
   EXPECT_FALSE(validate(inst));

   inst = make_mad(GEN_TYPE_W);
   inst.dst.type = GEN_TYPE_W;
   inst.dst.subnr = 0;
   inst.dst.region.hstride = 1;
   for (unsigned i = 0; i < 3; i++) {
      inst.src[i].type = GEN_TYPE_W;
      inst.src[i].region.vstride = 2;
      inst.src[i].region.width = 1;
      inst.src[i].region.hstride = 0;
      inst.src[i].subnr = 0;
   }
   inst.src[1].subnr = 4;
   EXPECT_FALSE(validate(inst));
}

TEST_P(gen_validate_test, xe2_register_region_special_restrictions_for_src0_and_src1)
{
   if (devinfo.verx10 < 200)
      GTEST_SKIP();

   const unsigned V = GEN_VSTRIDE_ONE_DIMENSIONAL;

   const struct {
      special_region_dst dst;
      special_region_src src0;
      special_region_src src1;
      bool expected_result;
   } tests[] = {
      /* Source 0. One element per dword channel. */
      { { GEN_TYPE_D, 0, 1 }, { GEN_TYPE_D, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_D, 0, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_D, 0, 1 }, { GEN_TYPE_B, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },

      { { GEN_TYPE_W, 0, 2 }, { GEN_TYPE_D, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 0, 2 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 0, 2 }, { GEN_TYPE_B, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },

      { { GEN_TYPE_B, 0, 4 }, { GEN_TYPE_D, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_B, 0, 4 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_B, 0, 4 }, { GEN_TYPE_B, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },

      { { GEN_TYPE_D, 0, 1 }, { GEN_TYPE_D, 0, V, 8, 1, true }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_D, 0, 1 }, { GEN_TYPE_D, 0, V, 1, 0, true }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },

      /* Source 0. Uniform stride W->W cases. */
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 2, 1, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 0, 2, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 2, 2, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 0, 4, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 2, 4, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },

      /* Source 0. Dword aligned W->W cases. */
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 0, 8, 4, 1, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 4, 8, 4, 1, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 0, 8, 4, 2, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 4, 8, 4, 2, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 0, 16, 2, 4, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 4, 16, 2, 4, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },

      /* Source 0. Uniform stride W->B cases. */
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_W, 1, 1, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_W, 0, 2, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_W, 1, 2, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_W, 0, 4, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_W, 1, 4, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },

      /* Source 0. Dword aligned W->B cases. */
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 0, 8, 4, 1, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 2, 8, 4, 1, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 0, 8, 4, 2, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 2, 8, 4, 2, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 0, 16, 2, 4, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 2, 16, 2, 4, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, false },

      /* Source 1. One element per dword channel. */
      { { GEN_TYPE_D, 0, 1 }, { GEN_TYPE_D, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_D, 0, 1 }, { GEN_TYPE_D, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 0, 2 }, { GEN_TYPE_D, 0, 1, 1, 0, false }, { GEN_TYPE_D, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 0, 2 }, { GEN_TYPE_D, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },

      /* Source 1. Uniform stride W->W cases. */
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 2, 1, 1, 0, false }, true },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 2, 1, 0, false }, false },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 2, 2, 1, 0, false }, true },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 4, 1, 0, false }, false },
      { { GEN_TYPE_W, 1, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 2, 4, 1, 0, false }, false },

      /* Source 1. Dword aligned W->W cases. */
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 8, 4, 1, false }, true },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 4, 8, 4, 1, false }, true },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 8, 4, 2, false }, false },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 4, 8, 4, 2, false }, true },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 16, 2, 4, false }, false },
      { { GEN_TYPE_W, 2, 1 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 4, 16, 2, 4, false }, false },

      /* Source 1. Uniform stride W->B cases. */
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_B, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 1, 1, 0, false }, true },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_B, 0, 1, 1, 0, false }, { GEN_TYPE_W, 1, 1, 1, 0, false }, true },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_B, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 2, 1, 0, false }, false },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_B, 0, 1, 1, 0, false }, { GEN_TYPE_W, 1, 2, 1, 0, false }, false },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_B, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 4, 1, 0, false }, false },
      { { GEN_TYPE_B, 2, 2 }, { GEN_TYPE_B, 0, 1, 1, 0, false }, { GEN_TYPE_W, 1, 4, 1, 0, false }, false },

      /* Source 1. Dword aligned W->B cases. */
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 8, 4, 1, false }, true },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 2, 8, 4, 1, false }, true },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 8, 4, 2, false }, false },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 2, 8, 4, 2, false }, false },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 0, 16, 2, 4, false }, false },
      { { GEN_TYPE_B, 4, 2 }, { GEN_TYPE_W, 0, 1, 1, 0, false }, { GEN_TYPE_W, 2, 16, 2, 4, false }, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
      SCOPED_TRACE(::testing::Message() << "test vector index = " << i);
      const gen_inst inst = make_special_region_add(tests[i].dst, tests[i].src0, tests[i].src1);
      EXPECT_EQ(tests[i].expected_result, validate(inst));
   }
}

TEST_P(gen_validate_test, scalar_register_restrictions)
{
   gen_inst inst = make_mov(GEN_TYPE_UD, GEN_TYPE_UD, 1);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_SCALAR;
   inst.src[0].file = GEN_GRF;
   inst.src[0].nr = 1;
   inst.src[0].region.vstride = 0;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 0;

   if (devinfo.ver < 30) {
      EXPECT_FALSE(validate(inst));
      return;
   }

   EXPECT_TRUE(validate(inst));

   inst = make_add(GEN_TYPE_UD);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_SCALAR;
   EXPECT_FALSE(validate(inst));

   inst = make_mov(GEN_TYPE_UQ, GEN_TYPE_UQ, 1);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_SCALAR;
   inst.src[0].file = GEN_IMM;
   EXPECT_TRUE(validate(inst));

   inst = make_mov(GEN_TYPE_UQ, GEN_TYPE_UD, 1);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_SCALAR;
   inst.src[0].file = GEN_IMM;
   EXPECT_FALSE(validate(inst));

   inst = make_mov(GEN_TYPE_UW, GEN_TYPE_UW, 8);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_SCALAR;
   inst.src[0].file = GEN_IMM;
   EXPECT_FALSE(validate(inst));

   inst = make_mov(GEN_TYPE_UW, GEN_TYPE_UW, 1);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_SCALAR;
   inst.src[0].file = GEN_IMM;
   inst.cmod = GEN_CONDITION_ZE;
   EXPECT_FALSE(validate(inst));

   inst = make_mov(GEN_TYPE_UD, GEN_TYPE_UD, 1, 28);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_SCALAR;
   inst.src[0].file = GEN_GRF;
   inst.src[0].nr = 1;
   inst.src[0].region.vstride = 0;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 0;
   EXPECT_FALSE(validate(inst));

   inst = make_mov(GEN_TYPE_UW, GEN_TYPE_UW, 8);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_SCALAR;
   inst.src[0].region.vstride = 0;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 0;
   EXPECT_TRUE(validate(inst));

   inst = make_mov(GEN_TYPE_UW, GEN_TYPE_UW, 8);
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_SCALAR;
   EXPECT_FALSE(validate(inst));

   inst = make_mov(GEN_TYPE_UW, GEN_TYPE_UW, 1);
   inst.dst.file = GEN_ARF;
   inst.dst.nr = GEN_ARF_SCALAR;
   inst.src[0].file = GEN_ARF;
   inst.src[0].nr = GEN_ARF_SCALAR;
   inst.src[0].region.vstride = 0;
   inst.src[0].region.width = 1;
   inst.src[0].region.hstride = 0;
   EXPECT_FALSE(validate(inst));

   for (gen_opcode op : { GEN_OP_SEND, GEN_OP_SENDC }) {
      SCOPED_TRACE(::testing::Message() << "opcode=" << unsigned(op));

      inst = make_send(op);
      inst.src[0].file = GEN_ARF;
      inst.src[0].nr = GEN_ARF_SCALAR;
      inst.src[0].subnr = 0;
      EXPECT_TRUE(validate(inst));

      inst = make_send(op);
      inst.src[0].file = GEN_ARF;
      inst.src[0].nr = GEN_ARF_SCALAR;
      inst.src[0].subnr = 1;
      EXPECT_FALSE(validate(inst));

      inst = make_send(op);
      inst.src[0].file = GEN_ARF;
      inst.src[0].nr = GEN_ARF_SCALAR;
      inst.src[0].subnr = 0;
      inst.src[1].file = GEN_GRF;
      inst.src[1].nr = 2;
      EXPECT_FALSE(validate(inst));
   }

   inst = make_send(GEN_OP_SEND);
   inst.src[1].file = GEN_ARF;
   inst.src[1].nr = GEN_ARF_SCALAR;
   EXPECT_FALSE(validate(inst));
}
