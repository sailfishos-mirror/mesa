/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <vector>

#include "brw_shader.h"
#include "gen/gen.h"

class brw_generator
{
public:
   brw_generator(const struct brw_compiler *compiler,
                const struct brw_compile_params *params,
                struct brw_stage_prog_data *prog_data,
                mesa_shader_stage stage);
   ~brw_generator();

   void enable_debug(const char *shader_name);
   int generate_code(const brw_shader &s,
                     struct genisa_stats *stats);
   void add_const_data(void *data, unsigned size);
   void add_resume_sbt(unsigned num_resume_shaders, uint64_t *sbt);
   const unsigned *get_assembly();

private:
   void generate_send(brw_send_inst *inst,
                      struct brw_reg dst,
                      struct brw_reg desc,
                      struct brw_reg ex_desc,
                      struct brw_reg payload,
                      struct brw_reg payload2,
                      bool ex_bso);
   void generate_barrier(brw_inst *inst, struct brw_reg src);
   void generate_ddx(const brw_inst *inst,
                     struct brw_reg dst, struct brw_reg src);
   void generate_ddy(const brw_inst *inst,
                     struct brw_reg dst, struct brw_reg src);
   void generate_scratch_header(brw_inst *inst,
                                struct brw_reg dst, struct brw_reg src);

   void generate_mov_indirect(brw_inst *inst,
                              struct brw_reg dst,
                              struct brw_reg reg,
                              struct brw_reg indirect_byte_offset);

   void generate_shuffle(brw_inst *inst,
                         struct brw_reg dst,
                         struct brw_reg src,
                         struct brw_reg idx);

   void generate_quad_swizzle(const brw_inst *inst,
                              struct brw_reg dst, struct brw_reg src,
                              unsigned swiz);

   void generate_broadcast(const brw_reg &dst, const brw_reg &src, const brw_reg &idx);
   void generate_math(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1, gen_math func);

   void generate_float_controls_mode(unsigned mode, unsigned mask);

   const struct brw_compiler *compiler;
   const struct brw_compile_params *params;

   const struct intel_device_info *devinfo;

   struct brw_stage_prog_data * const prog_data;

   unsigned dispatch_width; /**< 8, 16 or 32 */

   int final_halt_idx;
   bool needs_final_halt;

   bool debug_flag;
   const char *shader_name;
   mesa_shader_stage stage;
   void *mem_ctx;

   int output_size = 0;
   uint8_t *output = NULL;

   int allocate_output(unsigned size, unsigned alignemnt);
   int append_output(void *data, unsigned size, unsigned alignment);

   std::vector<gen_inst> gen_insts;

   const char *next_annotation;
   std::vector<const char *> annotations;

   struct state {
      uint8_t exec_size;
      uint8_t chan_offset;
      uint8_t flag_nr;
      uint8_t flag_subnr;
      gen_predicate pred_control;
      bool pred_inv;
      bool no_mask;
      bool saturate;
      bool align16;
      bool acc_wr_control;
      gen_swsb swsb;
   };

   std::vector<state> state_stack;

   void reset_state()     { state_stack.clear(); state_stack.push_back({}); }
   state *current_state() { return &state_stack.back(); }
   state *push_state()    { state_stack.push_back(*current_state()); return current_state(); }
   void pop_state()       { state_stack.pop_back(); }

   gen_operand to_gen(const brw_reg &r, bool align16 = false);
   gen_opcode to_gen(enum opcode op);
   gen_file to_gen(brw_reg_file file);

   gen_inst make_empty();
   gen_inst make(gen_opcode op);
   gen_inst *append(const gen_inst &gen);

   gen_inst *append(enum opcode opcode);
   gen_inst *append(enum opcode opcode, const brw_reg &dst,
                    const brw_reg &src0);
   gen_inst *append(enum opcode opcode, const brw_reg &dst,
                    const brw_reg &src0, const brw_reg &src1);
   gen_inst *append(enum opcode opcode, const brw_reg &dst,
                    const brw_reg &src0, const brw_reg &src1, const brw_reg &src2);

   gen_inst *append_SYNC(gen_sync_func func);
   gen_inst *append_NOP();

   inline gen_inst *append_MOV(const brw_reg &dst, const brw_reg &src0) { return append(BRW_OPCODE_MOV, dst, src0); }

   void update_branch_ips();

   int num_relocs = 0;
   intel_shader_reloc *relocs = NULL;

   void append_reloc(const intel_shader_reloc &r);
};
