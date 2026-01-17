/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef NIR_ALGEBRAIC_PATTERN_TEST_H
#define NIR_ALGEBRAIC_PATTERN_TEST_H

#include "nir.h"
#include "nir_test.h"
#include "nir_search.h"
#include "gtest/gtest-spi.h"

class nir_algebraic_pattern_test_variable_cond {
public:
   nir_algebraic_pattern_test_variable_cond(nir_alu_instr *alu, unsigned src_index, const nir_search_variable_cond cond)
       : alu(alu), src_index(src_index), cond(cond)
   {
   }

   nir_alu_instr *alu;
   unsigned src_index;
   const nir_search_variable_cond cond;
};

enum input_type {
   BOOL,
   INT,
   UINT,
   FLOAT,
};

class nir_algebraic_pattern_test_input {
 public:
   nir_algebraic_pattern_test_input(nir_load_const_instr *instr, input_type ty, uint32_t fuzzing_start_bit)
       : instr(instr), ty(ty), fuzzing_start_bit(fuzzing_start_bit)
   {
   }

   nir_load_const_instr *instr;
   input_type ty;
   uint32_t fuzzing_start_bit;
};

enum expected_result {
   PASS,
   XFAIL,
   UNSUPPORTED,
};

class nir_algebraic_pattern_test : public nir_test {
 protected:
   nir_algebraic_pattern_test(const char *name);
   virtual ~nir_algebraic_pattern_test();

   void set_inputs(uint32_t seed);
   bool check_variable_conds();
   void validate_pattern();

 public:
   std::vector<nir_algebraic_pattern_test_input> inputs;
   uint32_t fuzzing_bits;
   bool exact = true;
   enum expected_result expected_result = PASS;
   const char *expression_cond_failed = NULL;
   nir_fp_math_control fp_math_ctrl = (nir_fp_math_control)(nir_fp_preserve_signed_zero |
                                                            nir_fp_preserve_inf |
                                                            nir_fp_preserve_nan);
   std::vector<nir_algebraic_pattern_test_variable_cond> variable_conds;
   std::vector<nir_const_value> tmp_values;
};

/* Builders that aren't auto-generated for nir_builder.h, due to not being
 * having a defined dest size (3 or 4 components, independent of src args).
 * Just pick 4 and get some coverage.
 */
static inline nir_def *
nir_fdot_replicated(nir_builder *b, nir_op op, nir_def *x, nir_def *y)
{
   nir_alu_instr *alu = nir_alu_instr_create(b->shader, op);
   alu->src[0].src = nir_src_for_ssa(x);
   alu->src[1].src = nir_src_for_ssa(y);
   alu->fp_math_ctrl = b->fp_math_ctrl;
   nir_def_init(&alu->instr, &alu->def, 4, x->bit_size);
   nir_builder_instr_insert(b, &alu->instr);

   return &alu->def;
}

static inline nir_def *
nir_fdot2_replicated(nir_builder *b, nir_def *x, nir_def *y)
{
   return nir_fdot_replicated(b, nir_op_fdot2_replicated, x, y);
}

static inline nir_def *
nir_fdot3_replicated(nir_builder *b, nir_def *x, nir_def *y)
{
   return nir_fdot_replicated(b, nir_op_fdot3_replicated, x, y);
}

static inline nir_def *
nir_fdot4_replicated(nir_builder *b, nir_def *x, nir_def *y)
{
   return nir_fdot_replicated(b, nir_op_fdot4_replicated, x, y);
}

static inline nir_def *
nir_fdph_replicated(nir_builder *b, nir_def *x, nir_def *y)
{
   return nir_fdot_replicated(b, nir_op_fdph_replicated, x, y);
}

#endif
