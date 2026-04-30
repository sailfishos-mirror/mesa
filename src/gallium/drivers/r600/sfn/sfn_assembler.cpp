/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "sfn_assembler.h"

#include "../eg_sq.h"
#include "../r600_asm.h"

#include "sfn_callstack.h"
#include "sfn_conditionaljumptracker.h"
#include "sfn_debug.h"
#include "sfn_fill_bytecode.h"
#include "sfn_instr_alugroup.h"
#include "sfn_instr_controlflow.h"
#include "sfn_instr_export.h"
#include "sfn_instr_fetch.h"
#include "sfn_instr_mem.h"
#include "sfn_instr_tex.h"

#include <tuple>

namespace r600 {
Assembler::Assembler(r600_shader *sh, const r600_shader_key& key):
    m_sh(sh),
    m_key(key)
{
}

extern const std::map<ESDOp, int> ds_opcode_map;

class AssemblerVisitor : public ConstInstrVisitor {
public:
   AssemblerVisitor(r600_shader& sh, const r600_shader_key& key, bool legacy_math_rules);

   void visit(const AluInstr& instr) override;
   void visit(const AluGroup& instr) override;
   void visit(const TexInstr& instr) override;
   void visit(const ExportInstr& instr) override;
   void visit(const FetchInstr& instr) override;
   void visit(const Block& instr) override;
   void visit(const IfInstr& instr) override;
   void visit(const ControlFlowInstr& instr) override;
   void visit(const ScratchIOInstr& instr) override;
   void visit(const StreamOutInstr& instr) override;
   void visit(const MemRingOutInstr& instr) override;
   void visit(const EmitVertexInstr& instr) override;
   void visit(const GDSInstr& instr) override;
   void visit(const WriteTFInstr& instr) override;
   void visit(const LDSAtomicInstr& instr) override;
   void visit(const LDSReadInstr& instr) override;
   void visit(const RatInstr& instr) override;

   void finalize();

   const uint32_t sf_vtx = 1;
   const uint32_t sf_tex = 2;
   const uint32_t sf_alu = 4;
   const uint32_t sf_addr_register = 8;
   const uint32_t sf_all = 0xf;

   void clear_states(const uint32_t& states);

   void emit_endif();
   void emit_else();
   void emit_loop_begin(bool vpm);
   void emit_loop_end();
   void emit_loop_break();
   void emit_loop_cont();
   void emit_alu_push_before();

   void emit_alu_op(const AluInstr& ai);
   void emit_lds_op(const AluInstr& lds);
   auto get_lds_opcode_properties(const AluInstr& lds) const
      -> std::tuple<unsigned int, unsigned int, bool>;
   void update_alu_state_after_emit(EAluOp opcode,
                                    int dst_sel,
                                    int dst_chan);

   auto translate_for_mathrules(EAluOp op) -> EAluOp;

   void emit_wait_ack();

   /* Start initialized in constructor */
   const r600_shader_key& m_key;
   r600_shader& m_shader;
   r600_bytecode& m_bc;

   ConditionalJumpTracker m_jump_tracker;
   CallStack m_callstack;
   bool ps_alpha_to_one;
   bool ps_alpha_to_one_and_coverage;
   /* End initialized in constructor */
   
   std::set<int> vtx_fetch_results;
   std::set<int> tex_fetch_results;

   const VirtualValue *m_last_addr{nullptr};

   unsigned m_max_color_exports{0};
   int m_loop_nesting{0};

   bool m_ack_suggested{false};
   bool m_has_param_output{false};
   bool m_has_pos_output{false};
   bool m_last_op_was_barrier{false};
   bool m_result{true};
   bool m_legacy_math_rules{false};
   bool m_require_alu_extended{false};
};

bool
Assembler::lower(Shader *shader)
{
   AssemblerVisitor ass(*m_sh, m_key, shader->has_flag(Shader::sh_legacy_math_rules));

   auto& blocks = shader->func();
   for (auto b : blocks) {
      b->accept(ass);
      if (!ass.m_result)
         return false;
   }

   ass.finalize();

   return ass.m_result;
}

AssemblerVisitor::AssemblerVisitor(r600_shader& sh, const r600_shader_key& key,
                                   bool legacy_math_rules):
    m_key(key),
    m_shader(sh),

    m_bc(sh.bc),
    m_callstack(sh.bc),
    ps_alpha_to_one(key.ps.alpha_to_one),
    ps_alpha_to_one_and_coverage(key.ps.alpha_to_one_and_coverage),
    m_legacy_math_rules(legacy_math_rules)
{
   if (m_shader.processor_type == MESA_SHADER_FRAGMENT)
      m_max_color_exports = MAX2(m_key.ps.nr_cbufs, 1);

   if (m_shader.processor_type == MESA_SHADER_VERTEX && m_shader.ninput > 0)
      r600_bytecode_add_cfinst(&m_bc, CF_OP_CALL_FS);
}

void
AssemblerVisitor::finalize()
{
   const struct cf_op_info *last = nullptr;

   if (m_bc.cf_last)
      last = r600_isa_cf(m_bc.cf_last->op);

   /* alu clause instructions don't have EOP bit, so add NOP */
    if (m_shader.bc.gfx_level < CAYMAN &&
       (!last || last->flags & CF_ALU || m_bc.cf_last->op == CF_OP_LOOP_END ||
        m_bc.cf_last->op == CF_OP_POP))
      r600_bytecode_add_cfinst(&m_bc, CF_OP_NOP);

   /* A fetch shader only can't be EOP (results in hang), but we can replace
    * it by a NOP */
   else if (last && m_bc.cf_last->op == CF_OP_CALL_FS)
      m_bc.cf_last->op = CF_OP_NOP;

   if (m_shader.bc.gfx_level != CAYMAN)
      m_bc.cf_last->end_of_program = 1;
   else
      cm_bytecode_add_cf_end(&m_bc);
}

extern const std::map<EAluOp, int> opcode_map;

void
AssemblerVisitor::visit(const AluInstr& ai)
{
   assert(vtx_fetch_results.empty());
   assert(tex_fetch_results.empty());

   if (unlikely(ai.has_alu_flag(alu_is_lds)))
      emit_lds_op(ai);
   else
      emit_alu_op(ai);
}

void
AssemblerVisitor::emit_lds_op(const AluInstr& lds)
{
   struct r600_bytecode_alu alu;
   memset(&alu, 0, sizeof(alu));

   alu.is_lds_idx_op = true;
   auto [opcode, lds_idx, has_lds_fetch] = get_lds_opcode_properties(lds);
   alu.op = opcode;
   alu.lds_idx = lds_idx;

   fill_alu_src(alu.src[0], lds.src(0), m_bc);

   if (lds.n_sources() > 1)
      fill_alu_src(alu.src[1], lds.src(1), m_bc);
   else
      alu.src[1].sel = V_SQ_ALU_SRC_0;

   if (lds.n_sources() > 2)
      fill_alu_src(alu.src[2], lds.src(2), m_bc);
   else
      alu.src[2].sel = V_SQ_ALU_SRC_0;

   alu.last = lds.has_alu_flag(alu_last_instr);
   int r = r600_bytecode_add_alu(&m_bc, &alu);
   if (has_lds_fetch)
      m_bc.cf_last->nlds_read++;

   if (r)
      m_result = false;
}

auto
AssemblerVisitor::get_lds_opcode_properties(const AluInstr& lds) const
   -> std::tuple<unsigned int, unsigned int, bool>
{
   unsigned int opcode = lds.lds_opcode();
   unsigned int lds_idx = 0;
   bool has_lds_fetch = false;

   switch (opcode) {
   case LDS_WRITE:
      opcode = LDS_OP2_LDS_WRITE;
      break;
   case LDS_WRITE_REL:
      opcode = LDS_OP3_LDS_WRITE_REL;
      lds_idx = 1;
      break;
   case DS_OP_READ_RET:
      opcode = LDS_OP1_LDS_READ_RET;
      FALLTHROUGH;
   case LDS_ADD_RET:
   case LDS_AND_RET:
   case LDS_OR_RET:
   case LDS_MAX_INT_RET:
   case LDS_MAX_UINT_RET:
   case LDS_MIN_INT_RET:
   case LDS_MIN_UINT_RET:
   case LDS_XOR_RET:
   case LDS_XCHG_RET:
   case LDS_CMP_XCHG_RET:
      has_lds_fetch = true;
      break;
   case LDS_ADD:
   case LDS_AND:
   case LDS_OR:
   case LDS_MAX_INT:
   case LDS_MAX_UINT:
   case LDS_MIN_INT:
   case LDS_MIN_UINT:
   case LDS_XOR:
      break;
   default:
      std::cerr << "\n R600: error op: " << lds << "\n";
      UNREACHABLE("Unhandled LDS op");
   }

   return std::make_tuple(opcode, lds_idx, has_lds_fetch);
}

auto AssemblerVisitor::translate_for_mathrules(EAluOp op) -> EAluOp
{
   switch (op) {
   case op2_dot_ieee: return op2_dot;
   case op2_dot4_ieee: return op2_dot4;
   case op2_mul_ieee: return op2_mul;
   case op3_muladd_ieee : return op2_mul_ieee;
   default:
      return op;
   }
}

void
AssemblerVisitor::emit_alu_op(const AluInstr& ai)
{
   sfn_log << SfnLog::assembly << "Emit ALU op " << ai << "\n";

   struct r600_bytecode_alu alu;
   memset(&alu, 0, sizeof(alu));

   auto opcode = ai.opcode();

   if (unlikely(opcode == op1_mova_int &&
                (m_bc.gfx_level < CAYMAN || alu.dst.sel == 0))) {
      m_last_addr = ai.psrc(0);
      m_bc.ar_chan = m_last_addr->chan();
      m_bc.ar_reg = m_last_addr->sel();
   }

   if (m_legacy_math_rules)
       opcode = translate_for_mathrules(opcode);

   auto hw_opcode = opcode_map.find(opcode);

   if (hw_opcode == opcode_map.end()) {
      std::cerr << "Opcode not handled for " << ai << "\n";
      m_result = false;
      return;
   }

   // skip multiple barriers
   if (m_last_op_was_barrier && opcode == op0_group_barrier)
      return;

   m_last_op_was_barrier = opcode == op0_group_barrier;

   alu.op = hw_opcode->second;
   alu.is_op3 = ai.n_sources() == 3;
   alu.omod = !alu.is_op3 ? ai.output_modifier() : 0;
   alu.last = ai.has_alu_flag(alu_last_instr);
   alu.execute_mask = ai.has_alu_flag(alu_update_exec);
   if (ai.bank_swizzle() != alu_vec_unknown)
      alu.bank_swizzle_force = ai.bank_swizzle();

   if (!fill_alu_dst(alu, ai, m_bc, m_last_addr)) {
      m_result = false;
      return;
   }

   fill_alu_src_operands(alu, ai, m_bc);

   if (ai.has_lds_queue_read()) {
      assert(m_bc.cf_last->nlds_read > 0);
      m_bc.cf_last->nlds_read--;
   }

   if (m_last_addr)
      sfn_log << SfnLog::assembly << "  Current address register is " << *m_last_addr
              << "\n";


   m_result = !r600_bytecode_add_alu(&m_bc, &alu);

   update_alu_state_after_emit(opcode, alu.dst.sel, alu.dst.chan);
}

void
AssemblerVisitor::update_alu_state_after_emit(EAluOp opcode,
                                              int dst_sel,
                                              int dst_chan)
{

   switch (opcode) {
   case op1_mova_int:
      if (m_bc.gfx_level < CAYMAN || dst_sel == 0) {
         m_bc.ar_loaded = 1;
      } else if (m_bc.gfx_level == CAYMAN) {
         int idx = dst_sel - 2;
         m_bc.index_loaded[idx] = 1;
         m_bc.index_reg[idx] = -1;
      }
      break;
   case op1_set_cf_idx0:
      m_bc.index_loaded[0] = 1;
      m_bc.index_reg[0] = -1;
      break;
   case op1_set_cf_idx1:
      m_bc.index_loaded[1] = 1;
      m_bc.index_reg[1] = -1;
      break;
   default:
      break;
   }

   if (dst_sel >= g_clause_local_start && dst_sel < g_clause_local_end) {
      int clidx = 4 * (dst_sel - g_clause_local_start) + dst_chan;
      m_bc.cf_last->clause_local_written |= 1 << clidx;
   }
}

void
AssemblerVisitor::visit(const AluGroup& group)
{
   clear_states(sf_vtx | sf_tex);

   if (group.slots() == 0)
      return;

   static const unsigned slot_limit = 256;

   if (m_bc.cf_last && !m_bc.force_add_cf) {
      if (group.has_lds_group_start()) {
         if (m_bc.cf_last->ndw + 2 * (*group.begin())->required_slots() > slot_limit) {
            assert(m_bc.cf_last->nlds_read == 0);
            assert(0 && "Not allowed to start new alu group here");
            m_bc.force_add_cf = 1;
            m_last_addr = nullptr;
         }
      } else {
         if (m_bc.cf_last->ndw + 2 * group.slots() > slot_limit) {
            std::cerr << "m_bc.cf_last->ndw = " << m_bc.cf_last->ndw
                      << " group.slots() = " << group.slots()
                      << " -> " << m_bc.cf_last->ndw + 2 * group.slots()
                      << "> slot_limit = " << slot_limit << "\n";
            assert(m_bc.cf_last->nlds_read == 0);
            assert(0 && "Not allowed to start new alu group here");
            m_bc.force_add_cf = 1;
            m_last_addr = nullptr;
         } else {
            auto instr = *group.begin();
            if (instr && !instr->has_alu_flag(alu_is_lds) &&
                instr->opcode() == op0_group_barrier && m_bc.cf_last->ndw + 14 > slot_limit) {
               assert(0 && "Not allowed to start new alu group here");
               assert(m_bc.cf_last->nlds_read == 0);
               m_bc.force_add_cf = 1;
               m_last_addr = nullptr;
            }
         }
      }
   }

   auto [addr, is_index] = group.addr();
   assert(!addr || addr->has_flag(Register::addr_or_idx));

   for (auto& i : group) {
      if (i)
         i->accept(*this);
   }
}

void
AssemblerVisitor::visit(const TexInstr& tex_instr)
{
   clear_states(sf_vtx | sf_alu);

   if (tex_fetch_results.find(tex_instr.src().sel()) != tex_fetch_results.end()) {
      m_bc.force_add_cf = 1;
      tex_fetch_results.clear();
   }

   if (tex_instr.dest_swizzle(0) < 4 && tex_instr.dest_swizzle(1) < 4 &&
       tex_instr.dest_swizzle(2) < 4 && tex_instr.dest_swizzle(3) < 4)
      tex_fetch_results.insert(tex_instr.dst().sel());

   m_result &= emit_bytecode_tex(m_bc, tex_instr);
}

void
AssemblerVisitor::visit(const ExportInstr& exi)
{
   if (unlikely(ps_alpha_to_one_and_coverage && exi.export_type() == ExportInstr::pixel &&
                exi.location() == 0)) {
      m_result &= emit_bytecode_export_alpha_to_coverage(m_bc, exi);
   }

   clear_states(sf_all);
   m_result &= emit_bytecode_export(m_bc, exi, ps_alpha_to_one);
}

void
AssemblerVisitor::visit(const ScratchIOInstr& instr)
{
   clear_states(sf_all);

   m_result &= emit_bytecode_scratch(m_bc, instr);
}

void
AssemblerVisitor::visit(const StreamOutInstr& instr)
{
   m_result &= emit_bytecode_stream_out(m_bc, instr);
}

void
AssemblerVisitor::visit(const MemRingOutInstr& instr)
{
   m_result &= emit_bytecode_mem_ring(m_bc, instr);
}

void
AssemblerVisitor::visit(const EmitVertexInstr& instr)
{
   int r = r600_bytecode_add_cfinst(&m_bc, instr.op());
   if (!r)
      m_bc.cf_last->count = instr.stream();
   else
      m_result = false;
   assert(m_bc.cf_last->count < 4);
}

void
AssemblerVisitor::visit(const FetchInstr& fetch_instr)
{
   bool use_tc =
      fetch_instr.has_fetch_flag(FetchInstr::use_tc) || (m_bc.gfx_level == CAYMAN);

   auto clear_flags = use_tc ? sf_vtx : sf_tex;

   clear_states(clear_flags | sf_alu);

   if (fetch_instr.has_fetch_flag(FetchInstr::wait_ack))
      emit_wait_ack();


   if (!use_tc &&
       vtx_fetch_results.find(fetch_instr.src().sel()) != vtx_fetch_results.end()) {
      m_bc.force_add_cf = 1;
      vtx_fetch_results.clear();
   }

   if (fetch_instr.has_fetch_flag(FetchInstr::use_tc) &&
       tex_fetch_results.find(fetch_instr.src().sel()) != tex_fetch_results.end()) {
      m_bc.force_add_cf = 1;
      tex_fetch_results.clear();
   }

   if (use_tc)
      tex_fetch_results.insert(fetch_instr.dst().sel());
   else
      vtx_fetch_results.insert(fetch_instr.dst().sel());

   m_result &= emit_bytecode_fetch(m_bc, fetch_instr, use_tc);

   m_bc.cf_last->vpm =
      (m_bc.type == MESA_SHADER_FRAGMENT) && fetch_instr.has_fetch_flag(FetchInstr::vpm);
   m_bc.cf_last->barrier = 1;
}

void
AssemblerVisitor::visit(const WriteTFInstr& instr)
{
   m_result &= emit_bytecode_tf_write(m_bc, instr);
}

void
AssemblerVisitor::visit(const RatInstr& instr)
{
   /* The instruction writes to the return buffer location, and
    * the value will actually be read back, so make sure all previous writes
    * have been finished */
   if (m_ack_suggested)
      emit_wait_ack();

   r600_bytecode_add_cfinst(&m_bc, instr.cf_opcode());
   fill_bytecode_rat(*m_bc.cf_last, instr, m_shader.rat_base, m_bc.type);

   m_ack_suggested |= instr.need_ack();
}

void
AssemblerVisitor::clear_states(const uint32_t& states)
{
   if (states & sf_vtx)
      vtx_fetch_results.clear();

   if (states & sf_tex)
      tex_fetch_results.clear();

   if (states & sf_alu) {
      m_last_op_was_barrier = false;
      m_last_addr = nullptr;
   }
}

void
AssemblerVisitor::visit(const Block& block)
{
   if (block.empty())
      return;

   if (block.cf_start())
      block.cf_start()->accept(*this);
   else if (block.has_instr_flag(Instr::force_cf)) {
      m_bc.force_add_cf = 1;
      m_bc.ar_loaded = 0;
      m_last_addr = nullptr;
   }
   sfn_log << SfnLog::assembly << "Translate block  size: " << block.size()
           << " new_cf:" << m_bc.force_add_cf << "\n";

   m_require_alu_extended = block.kcache_needs_extended();
   for (const auto& i : block) {
      sfn_log << SfnLog::assembly << "Translate " << *i << " ";
      i->accept(*this);
      sfn_log << SfnLog::assembly << (m_result ? "good" : "fail") << "\n";

      if (!m_result)
         break;
   }
   m_require_alu_extended = false;
}

void
AssemblerVisitor::visit(const IfInstr& instr)
{

   auto pred = instr.predicate();
   auto [addr, dummy0, dummy1] = pred->indirect_addr();
   assert(!dummy1);
   assert(!addr);

   r600_bytecode_add_cfinst(&m_bc, CF_OP_JUMP);
   clear_states(sf_all);

   m_jump_tracker.push(m_bc.cf_last, jt_if);
}

void
AssemblerVisitor::visit(const ControlFlowInstr& instr)
{
   sfn_log << SfnLog::assembly << "Translate " << instr << " ";

   clear_states(sf_all);
   switch (instr.cf_type()) {
   case ControlFlowInstr::cf_else:
      emit_else();
      break;
   case ControlFlowInstr::cf_endif:
      emit_endif();
      break;
   case ControlFlowInstr::cf_loop_begin: {
      bool use_vpm = m_shader.processor_type == MESA_SHADER_FRAGMENT &&
                     instr.has_instr_flag(Instr::vpm) &&
                     !instr.has_instr_flag(Instr::helper);
      emit_loop_begin(use_vpm);
      break;
   }
   case ControlFlowInstr::cf_loop_end:
      emit_loop_end();
      break;
   case ControlFlowInstr::cf_loop_break:
      emit_loop_break();
      break;
   case ControlFlowInstr::cf_loop_continue:
      emit_loop_cont();
      break;
   case ControlFlowInstr::cf_wait_ack:
      emit_wait_ack();
      break;
   case ControlFlowInstr::cf_alu:
      r600_bytecode_add_cfinst(&m_bc, CF_OP_ALU);
      break;
   case ControlFlowInstr::cf_alu_push_before:
      emit_alu_push_before();
      break;
   case ControlFlowInstr::cf_gds:
      r600_bytecode_add_cfinst(&m_bc, CF_OP_GDS);
      break;
   case ControlFlowInstr::cf_tex:
      r600_bytecode_add_cfinst(&m_bc, CF_OP_TEX);
      break;
   case ControlFlowInstr::cf_vtx:
      r600_bytecode_add_cfinst(&m_bc, CF_OP_VTX);
      break;
   default:
      UNREACHABLE("Unknown CF instruction type");
   }
}

void
AssemblerVisitor::visit(const GDSInstr& instr)
{
   if (!(m_result &= emit_bytecode_gds(m_bc, instr)))
      return;

   m_bc.cf_last->vpm = MESA_SHADER_FRAGMENT == m_bc.type;
   m_bc.cf_last->barrier = 1;
}

void
AssemblerVisitor::visit(const LDSAtomicInstr& instr)
{
   (void)instr;
   UNREACHABLE("LDSAtomicInstr must be lowered to ALUInstr");
}

void
AssemblerVisitor::visit(const LDSReadInstr& instr)
{
   (void)instr;
   UNREACHABLE("LDSReadInstr must be lowered to ALUInstr");
}

void
AssemblerVisitor::emit_else()
{
   r600_bytecode_add_cfinst(&m_bc, CF_OP_ELSE);
   m_bc.cf_last->pop_count = 1;
   m_result &= m_jump_tracker.add_mid(m_bc.cf_last, jt_if);
}

void
AssemblerVisitor::emit_alu_push_before()
{
   int elems = m_callstack.push(FC_PUSH_VPM);
   bool needs_workaround = false;

   if (m_bc.gfx_level == CAYMAN && m_bc.stack.loop > 1)
      needs_workaround = true;

   if (m_bc.gfx_level == EVERGREEN && m_bc.family != CHIP_HEMLOCK &&
       m_bc.family != CHIP_CYPRESS && m_bc.family != CHIP_JUNIPER) {
      unsigned dmod1 = (elems - 1) % m_bc.stack.entry_size;
      unsigned dmod2 = (elems) % m_bc.stack.entry_size;

      if (elems && (!dmod1 || !dmod2))
         needs_workaround = true;
   }

   if (needs_workaround || m_require_alu_extended) {
      r600_bytecode_add_cfinst(&m_bc, CF_OP_PUSH);
      m_bc.cf_last->cf_addr = m_bc.cf_last->id + 2;
      r600_bytecode_add_cfinst(&m_bc, CF_OP_ALU);
      m_bc.cf_last->eg_alu_extended = m_require_alu_extended;
   } else {
      r600_bytecode_add_cfinst(&m_bc, CF_OP_ALU_PUSH_BEFORE);
   }

   clear_states(sf_tex | sf_vtx);
}

void
AssemblerVisitor::emit_endif()
{
   m_callstack.pop(FC_PUSH_VPM);

   unsigned force_pop = m_bc.force_add_cf;
   if (!force_pop) {
      int alu_pop = 3;
      if (m_bc.cf_last) {
         if (m_bc.cf_last->op == CF_OP_ALU)
            alu_pop = 0;
         else if (m_bc.cf_last->op == CF_OP_ALU_POP_AFTER)
            alu_pop = 1;
      }
      alu_pop += 1;
      if (alu_pop == 1) {
         m_bc.cf_last->op = CF_OP_ALU_POP_AFTER;
         m_bc.force_add_cf = 1;
      } else {
         force_pop = 1;
      }
   }

   if (force_pop) {
      r600_bytecode_add_cfinst(&m_bc, CF_OP_POP);
      m_bc.cf_last->pop_count = 1;
      m_bc.cf_last->cf_addr = m_bc.cf_last->id + 2;
   }

   m_result &= m_jump_tracker.pop(m_bc.cf_last, jt_if);
}

void
AssemblerVisitor::emit_loop_begin(bool vpm)
{
   r600_bytecode_add_cfinst(&m_bc, CF_OP_LOOP_START_DX10);
   m_bc.cf_last->vpm = vpm && m_bc.type == MESA_SHADER_FRAGMENT;
   m_jump_tracker.push(m_bc.cf_last, jt_loop);
   m_callstack.push(FC_LOOP);
   ++m_loop_nesting;
}

void
AssemblerVisitor::emit_loop_end()
{
   if (m_ack_suggested) {
      emit_wait_ack();
      m_ack_suggested = false;
   }

   r600_bytecode_add_cfinst(&m_bc, CF_OP_LOOP_END);
   m_callstack.pop(FC_LOOP);
   assert(m_loop_nesting);
   --m_loop_nesting;
   m_result |= m_jump_tracker.pop(m_bc.cf_last, jt_loop);
}

void
AssemblerVisitor::emit_loop_break()
{
   r600_bytecode_add_cfinst(&m_bc, CF_OP_LOOP_BREAK);
   m_result |= m_jump_tracker.add_mid(m_bc.cf_last, jt_loop);
}

void
AssemblerVisitor::emit_loop_cont()
{
   r600_bytecode_add_cfinst(&m_bc, CF_OP_LOOP_CONTINUE);
   m_result |= m_jump_tracker.add_mid(m_bc.cf_last, jt_loop);
}

void
AssemblerVisitor::emit_wait_ack()
{
   int r = r600_bytecode_add_cfinst(&m_bc, CF_OP_WAIT_ACK);
   if (!r) {
      m_bc.cf_last->cf_addr = 0;
      m_bc.cf_last->barrier = 1;
      m_ack_suggested = false;
   } else
      m_result = false;
}

const std::map<EAluOp, int> opcode_map = {

   {op2_add,                       ALU_OP2_ADD                      },
   {op2_mul,                       ALU_OP2_MUL                      },
   {op2_mul_ieee,                  ALU_OP2_MUL_IEEE                 },
   {op2_max,                       ALU_OP2_MAX                      },
   {op2_min,                       ALU_OP2_MIN                      },
   {op2_max_dx10,                  ALU_OP2_MAX_DX10                 },
   {op2_min_dx10,                  ALU_OP2_MIN_DX10                 },
   {op2_sete,                      ALU_OP2_SETE                     },
   {op2_setgt,                     ALU_OP2_SETGT                    },
   {op2_setge,                     ALU_OP2_SETGE                    },
   {op2_setne,                     ALU_OP2_SETNE                    },
   {op2_sete_dx10,                 ALU_OP2_SETE_DX10                },
   {op2_setgt_dx10,                ALU_OP2_SETGT_DX10               },
   {op2_setge_dx10,                ALU_OP2_SETGE_DX10               },
   {op2_setne_dx10,                ALU_OP2_SETNE_DX10               },
   {op1_fract,                     ALU_OP1_FRACT                    },
   {op1_trunc,                     ALU_OP1_TRUNC                    },
   {op1_ceil,                      ALU_OP1_CEIL                     },
   {op1_rndne,                     ALU_OP1_RNDNE                    },
   {op1_floor,                     ALU_OP1_FLOOR                    },
   {op2_ashr_int,                  ALU_OP2_ASHR_INT                 },
   {op2_lshr_int,                  ALU_OP2_LSHR_INT                 },
   {op2_lshl_int,                  ALU_OP2_LSHL_INT                 },
   {op1_mov,                       ALU_OP1_MOV                      },
   {op0_nop,                       ALU_OP0_NOP                      },
   {op2_mul_64,                    ALU_OP2_MUL_64                   },
   {op1v_flt64_to_flt32,           ALU_OP1_FLT64_TO_FLT32           },
   {op1v_flt32_to_flt64,           ALU_OP1_FLT32_TO_FLT64           },
   {op2_prede_int,                 ALU_OP2_PRED_SETE_INT            },
   {op2_pred_setne_int,            ALU_OP2_PRED_SETNE_INT           },
   {op2_pred_setge_int,            ALU_OP2_PRED_SETGE_INT           },
   {op2_pred_setgt_int,            ALU_OP2_PRED_SETGT_INT           },
   {op2_pred_setgt_uint,           ALU_OP2_PRED_SETGT_UINT          },
   {op2_pred_setge_uint,           ALU_OP2_PRED_SETGE_UINT          },
   {op2_pred_sete,                 ALU_OP2_PRED_SETE                },
   {op2_pred_setgt,                ALU_OP2_PRED_SETGT               },
   {op2_pred_setge,                ALU_OP2_PRED_SETGE               },
   {op2_pred_setne,                ALU_OP2_PRED_SETNE               },
   {op0_pred_set_clr,              ALU_OP0_PRED_SET_CLR             },
   {op1_pred_set_restore,          ALU_OP1_PRED_SET_RESTORE         },
   {op2_pred_sete_push,            ALU_OP2_PRED_SETE_PUSH           },
   {op2_pred_setgt_push,           ALU_OP2_PRED_SETGT_PUSH          },
   {op2_pred_setge_push,           ALU_OP2_PRED_SETGE_PUSH          },
   {op2_pred_setne_push,           ALU_OP2_PRED_SETNE_PUSH          },
   {op2_kille,                     ALU_OP2_KILLE                    },
   {op2_killgt,                    ALU_OP2_KILLGT                   },
   {op2_killge,                    ALU_OP2_KILLGE                   },
   {op2_killne,                    ALU_OP2_KILLNE                   },
   {op2_and_int,                   ALU_OP2_AND_INT                  },
   {op2_or_int,                    ALU_OP2_OR_INT                   },
   {op2_xor_int,                   ALU_OP2_XOR_INT                  },
   {op1_not_int,                   ALU_OP1_NOT_INT                  },
   {op2_add_int,                   ALU_OP2_ADD_INT                  },
   {op2_sub_int,                   ALU_OP2_SUB_INT                  },
   {op2_max_int,                   ALU_OP2_MAX_INT                  },
   {op2_min_int,                   ALU_OP2_MIN_INT                  },
   {op2_max_uint,                  ALU_OP2_MAX_UINT                 },
   {op2_min_uint,                  ALU_OP2_MIN_UINT                 },
   {op2_sete_int,                  ALU_OP2_SETE_INT                 },
   {op2_setgt_int,                 ALU_OP2_SETGT_INT                },
   {op2_setge_int,                 ALU_OP2_SETGE_INT                },
   {op2_setne_int,                 ALU_OP2_SETNE_INT                },
   {op2_setgt_uint,                ALU_OP2_SETGT_UINT               },
   {op2_setge_uint,                ALU_OP2_SETGE_UINT               },
   {op2_killgt_uint,               ALU_OP2_KILLGT_UINT              },
   {op2_killge_uint,               ALU_OP2_KILLGE_UINT              },
   {op2_pred_setgt_int,            ALU_OP2_PRED_SETGT_INT           },
   {op2_pred_setge_int,            ALU_OP2_PRED_SETGE_INT           },
   {op2_pred_setne_int,            ALU_OP2_PRED_SETNE_INT           },
   {op2_kille_int,                 ALU_OP2_KILLE_INT                },
   {op2_killgt_int,                ALU_OP2_KILLGT_INT               },
   {op2_killge_int,                ALU_OP2_KILLGE_INT               },
   {op2_killne_int,                ALU_OP2_KILLNE_INT               },
   {op2_pred_sete_push_int,        ALU_OP2_PRED_SETE_PUSH_INT       },
   {op2_pred_setgt_push_int,       ALU_OP2_PRED_SETGT_PUSH_INT      },
   {op2_pred_setge_push_int,       ALU_OP2_PRED_SETGE_PUSH_INT      },
   {op2_pred_setne_push_int,       ALU_OP2_PRED_SETNE_PUSH_INT      },
   {op2_pred_setlt_push_int,       ALU_OP2_PRED_SETLT_PUSH_INT      },
   {op2_pred_setle_push_int,       ALU_OP2_PRED_SETLE_PUSH_INT      },
   {op1_flt_to_int,                ALU_OP1_FLT_TO_INT               },
   {op1_bfrev_int,                 ALU_OP1_BFREV_INT                },
   {op2_addc_uint,                 ALU_OP2_ADDC_UINT                },
   {op2_subb_uint,                 ALU_OP2_SUBB_UINT                },
   {op0_group_barrier,             ALU_OP0_GROUP_BARRIER            },
   {op0_group_seq_begin,           ALU_OP0_GROUP_SEQ_BEGIN          },
   {op0_group_seq_end,             ALU_OP0_GROUP_SEQ_END            },
   {op2_set_mode,                  ALU_OP2_SET_MODE                 },
   {op1_set_cf_idx0,               ALU_OP0_SET_CF_IDX0              },
   {op1_set_cf_idx1,               ALU_OP0_SET_CF_IDX1              },
   {op2_set_lds_size,              ALU_OP2_SET_LDS_SIZE             },
   {op1_exp_ieee,                  ALU_OP1_EXP_IEEE                 },
   {op1_log_clamped,               ALU_OP1_LOG_CLAMPED              },
   {op1_log_ieee,                  ALU_OP1_LOG_IEEE                 },
   {op1_recip_clamped,             ALU_OP1_RECIP_CLAMPED            },
   {op1_recip_ff,                  ALU_OP1_RECIP_FF                 },
   {op1_recip_ieee,                ALU_OP1_RECIP_IEEE               },
   {op1_recipsqrt_clamped,         ALU_OP1_RECIPSQRT_CLAMPED        },
   {op1_recipsqrt_ff,              ALU_OP1_RECIPSQRT_FF             },
   {op1_recipsqrt_ieee1,           ALU_OP1_RECIPSQRT_IEEE           },
   {op1_sqrt_ieee,                 ALU_OP1_SQRT_IEEE                },
   {op1_sin,                       ALU_OP1_SIN                      },
   {op1_cos,                       ALU_OP1_COS                      },
   {op2_mullo_int,                 ALU_OP2_MULLO_INT                },
   {op2_mulhi_int,                 ALU_OP2_MULHI_INT                },
   {op2_mullo_uint,                ALU_OP2_MULLO_UINT               },
   {op2_mulhi_uint,                ALU_OP2_MULHI_UINT               },
   {op1_recip_int,                 ALU_OP1_RECIP_INT                },
   {op1_recip_uint,                ALU_OP1_RECIP_UINT               },
   {op1_recip_64,                  ALU_OP2_RECIP_64                 },
   {op1_recip_clamped_64,          ALU_OP2_RECIP_CLAMPED_64         },
   {op1_recipsqrt_64,              ALU_OP2_RECIPSQRT_64             },
   {op1_recipsqrt_clamped_64,      ALU_OP2_RECIPSQRT_CLAMPED_64     },
   {op1_sqrt_64,                   ALU_OP2_SQRT_64                  },
   {op1_flt_to_uint,               ALU_OP1_FLT_TO_UINT              },
   {op1_int_to_flt,                ALU_OP1_INT_TO_FLT               },
   {op1_uint_to_flt,               ALU_OP1_UINT_TO_FLT              },
   {op2_bfm_int,                   ALU_OP2_BFM_INT                  },
   {op1_flt32_to_flt16,            ALU_OP1_FLT32_TO_FLT16           },
   {op1_flt16_to_flt32,            ALU_OP1_FLT16_TO_FLT32           },
   {op1_ubyte0_flt,                ALU_OP1_UBYTE0_FLT               },
   {op1_ubyte1_flt,                ALU_OP1_UBYTE1_FLT               },
   {op1_ubyte2_flt,                ALU_OP1_UBYTE2_FLT               },
   {op1_ubyte3_flt,                ALU_OP1_UBYTE3_FLT               },
   {op1_bcnt_int,                  ALU_OP1_BCNT_INT                 },
   {op1_ffbh_uint,                 ALU_OP1_FFBH_UINT                },
   {op1_ffbl_int,                  ALU_OP1_FFBL_INT                 },
   {op1_ffbh_int,                  ALU_OP1_FFBH_INT                 },
   {op1_flt_to_uint4,              ALU_OP1_FLT_TO_UINT4             },
   {op2_dot_ieee,                  ALU_OP2_DOT_IEEE                 },
   {op1_flt_to_int_rpi,            ALU_OP1_FLT_TO_INT_RPI           },
   {op1_flt_to_int_floor,          ALU_OP1_FLT_TO_INT_FLOOR         },
   {op2_mulhi_uint24,              ALU_OP2_MULHI_UINT24             },
   {op1_mbcnt_32hi_int,            ALU_OP1_MBCNT_32HI_INT           },
   {op1_offset_to_flt,             ALU_OP1_OFFSET_TO_FLT            },
   {op2_mul_uint24,                ALU_OP2_MUL_UINT24               },
   {op1_bcnt_accum_prev_int,       ALU_OP1_BCNT_ACCUM_PREV_INT      },
   {op1_mbcnt_32lo_accum_prev_int, ALU_OP1_MBCNT_32LO_ACCUM_PREV_INT},
   {op2_sete_64,                   ALU_OP2_SETE_64                  },
   {op2_setne_64,                  ALU_OP2_SETNE_64                 },
   {op2_setgt_64,                  ALU_OP2_SETGT_64                 },
   {op2_setge_64,                  ALU_OP2_SETGE_64                 },
   {op2_min_64,                    ALU_OP2_MIN_64                   },
   {op2_max_64,                    ALU_OP2_MAX_64                   },
   {op2_dot4,                      ALU_OP2_DOT4                     },
   {op2_dot4_ieee,                 ALU_OP2_DOT4_IEEE                },
   {op2_cube,                      ALU_OP2_CUBE                     },
   {op1_max4,                      ALU_OP1_MAX4                     },
   {op1_frexp_64,                  ALU_OP1_FREXP_64                 },
   {op1_ldexp_64,                  ALU_OP2_LDEXP_64                 },
   {op1_fract_64,                  ALU_OP1_FRACT_64                 },
   {op2_pred_setgt_64,             ALU_OP2_PRED_SETGT_64            },
   {op2_pred_sete_64,              ALU_OP2_PRED_SETE_64             },
   {op2_pred_setge_64,             ALU_OP2_PRED_SETGE_64            },
   {op2_add_64,                    ALU_OP2_ADD_64                   },
   {op1_mova_int,                  ALU_OP1_MOVA_INT                 },
   {op1v_flt64_to_flt32,           ALU_OP1_FLT64_TO_FLT32           },
   {op1_flt32_to_flt64,            ALU_OP1_FLT32_TO_FLT64           },
   {op2_sad_accum_prev_uint,       ALU_OP2_SAD_ACCUM_PREV_UINT      },
   {op2_dot,                       ALU_OP2_DOT                      },
   {op1_mul_prev,                  ALU_OP1_MUL_PREV                 },
   {op1_mul_ieee_prev,             ALU_OP1_MUL_IEEE_PREV            },
   {op1_add_prev,                  ALU_OP1_ADD_PREV                 },
   {op2_muladd_prev,               ALU_OP2_MULADD_PREV              },
   {op2_muladd_ieee_prev,          ALU_OP2_MULADD_IEEE_PREV         },
   {op2_interp_xy,                 ALU_OP2_INTERP_XY                },
   {op2_interp_zw,                 ALU_OP2_INTERP_ZW                },
   {op2_interp_x,                  ALU_OP2_INTERP_X                 },
   {op2_interp_z,                  ALU_OP2_INTERP_Z                 },
   {op0_store_flags,               ALU_OP1_STORE_FLAGS              },
   {op1_load_store_flags,          ALU_OP1_LOAD_STORE_FLAGS         },
   {op0_lds_1a,                    ALU_OP2_LDS_1A                   },
   {op0_lds_1a1d,                  ALU_OP2_LDS_1A1D                 },
   {op0_lds_2a,                    ALU_OP2_LDS_2A                   },
   {op1_interp_load_p0,            ALU_OP1_INTERP_LOAD_P0           },
   {op1_interp_load_p10,           ALU_OP1_INTERP_LOAD_P10          },
   {op1_interp_load_p20,           ALU_OP1_INTERP_LOAD_P20          },
   {op3_bfe_uint,                  ALU_OP3_BFE_UINT                 },
   {op3_bfe_int,                   ALU_OP3_BFE_INT                  },
   {op3_bfi_int,                   ALU_OP3_BFI_INT                  },
   {op3_fma,                       ALU_OP3_FMA                      },
   {op3_cndne_64,                  ALU_OP3_CNDNE_64                 },
   {op3_fma_64,                    ALU_OP3_FMA_64                   },
   {op3_lerp_uint,                 ALU_OP3_LERP_UINT                },
   {op3_bit_align_int,             ALU_OP3_BIT_ALIGN_INT            },
   {op3_byte_align_int,            ALU_OP3_BYTE_ALIGN_INT           },
   {op3_sad_accum_uint,            ALU_OP3_SAD_ACCUM_UINT           },
   {op3_sad_accum_hi_uint,         ALU_OP3_SAD_ACCUM_HI_UINT        },
   {op3_muladd_uint24,             ALU_OP3_MULADD_UINT24            },
   {op3_lds_idx_op,                ALU_OP3_LDS_IDX_OP               },
   {op3_muladd,                    ALU_OP3_MULADD                   },
   {op3_muladd_m2,                 ALU_OP3_MULADD_M2                },
   {op3_muladd_m4,                 ALU_OP3_MULADD_M4                },
   {op3_muladd_d2,                 ALU_OP3_MULADD_D2                },
   {op3_muladd_ieee,               ALU_OP3_MULADD_IEEE              },
   {op3_cnde,                      ALU_OP3_CNDE                     },
   {op3_cndgt,                     ALU_OP3_CNDGT                    },
   {op3_cndge,                     ALU_OP3_CNDGE                    },
   {op3_cnde_int,                  ALU_OP3_CNDE_INT                 },
   {op3_cndgt_int,                 ALU_OP3_CNDGT_INT                },
   {op3_cndge_int,                 ALU_OP3_CNDGE_INT                },
   {op3_mul_lit,                   ALU_OP3_MUL_LIT                  },
};

const std::map<ESDOp, int> ds_opcode_map = {
   {DS_OP_ADD,                      FETCH_OP_GDS_ADD                 },
   {DS_OP_SUB,                      FETCH_OP_GDS_SUB                 },
   {DS_OP_RSUB,                     FETCH_OP_GDS_RSUB                },
   {DS_OP_INC,                      FETCH_OP_GDS_INC                 },
   {DS_OP_DEC,                      FETCH_OP_GDS_DEC                 },
   {DS_OP_MIN_INT,                  FETCH_OP_GDS_MIN_INT             },
   {DS_OP_MAX_INT,                  FETCH_OP_GDS_MAX_INT             },
   {DS_OP_MIN_UINT,                 FETCH_OP_GDS_MIN_UINT            },
   {DS_OP_MAX_UINT,                 FETCH_OP_GDS_MAX_UINT            },
   {DS_OP_AND,                      FETCH_OP_GDS_AND                 },
   {DS_OP_OR,                       FETCH_OP_GDS_OR                  },
   {DS_OP_XOR,                      FETCH_OP_GDS_XOR                 },
   {DS_OP_MSKOR,                    FETCH_OP_GDS_MSKOR               },
   {DS_OP_WRITE,                    FETCH_OP_GDS_WRITE               },
   {DS_OP_WRITE_REL,                FETCH_OP_GDS_WRITE_REL           },
   {DS_OP_WRITE2,                   FETCH_OP_GDS_WRITE2              },
   {DS_OP_CMP_STORE,                FETCH_OP_GDS_CMP_STORE           },
   {DS_OP_CMP_STORE_SPF,            FETCH_OP_GDS_CMP_STORE_SPF       },
   {DS_OP_BYTE_WRITE,               FETCH_OP_GDS_BYTE_WRITE          },
   {DS_OP_SHORT_WRITE,              FETCH_OP_GDS_SHORT_WRITE         },
   {DS_OP_ADD_RET,                  FETCH_OP_GDS_ADD_RET             },
   {DS_OP_SUB_RET,                  FETCH_OP_GDS_SUB_RET             },
   {DS_OP_RSUB_RET,                 FETCH_OP_GDS_RSUB_RET            },
   {DS_OP_INC_RET,                  FETCH_OP_GDS_INC_RET             },
   {DS_OP_DEC_RET,                  FETCH_OP_GDS_DEC_RET             },
   {DS_OP_MIN_INT_RET,              FETCH_OP_GDS_MIN_INT_RET         },
   {DS_OP_MAX_INT_RET,              FETCH_OP_GDS_MAX_INT_RET         },
   {DS_OP_MIN_UINT_RET,             FETCH_OP_GDS_MIN_UINT_RET        },
   {DS_OP_MAX_UINT_RET,             FETCH_OP_GDS_MAX_UINT_RET        },
   {DS_OP_AND_RET,                  FETCH_OP_GDS_AND_RET             },
   {DS_OP_OR_RET,                   FETCH_OP_GDS_OR_RET              },
   {DS_OP_XOR_RET,                  FETCH_OP_GDS_XOR_RET             },
   {DS_OP_MSKOR_RET,                FETCH_OP_GDS_MSKOR_RET           },
   {DS_OP_XCHG_RET,                 FETCH_OP_GDS_XCHG_RET            },
   {DS_OP_XCHG_REL_RET,             FETCH_OP_GDS_XCHG_REL_RET        },
   {DS_OP_XCHG2_RET,                FETCH_OP_GDS_XCHG2_RET           },
   {DS_OP_CMP_XCHG_RET,             FETCH_OP_GDS_CMP_XCHG_RET        },
   {DS_OP_CMP_XCHG_SPF_RET,         FETCH_OP_GDS_CMP_XCHG_SPF_RET    },
   {DS_OP_READ_RET,                 FETCH_OP_GDS_READ_RET            },
   {DS_OP_READ_REL_RET,             FETCH_OP_GDS_READ_REL_RET        },
   {DS_OP_READ2_RET,                FETCH_OP_GDS_READ2_RET           },
   {DS_OP_READWRITE_RET,            FETCH_OP_GDS_READWRITE_RET       },
   {DS_OP_BYTE_READ_RET,            FETCH_OP_GDS_BYTE_READ_RET       },
   {DS_OP_UBYTE_READ_RET,           FETCH_OP_GDS_UBYTE_READ_RET      },
   {DS_OP_SHORT_READ_RET,           FETCH_OP_GDS_SHORT_READ_RET      },
   {DS_OP_USHORT_READ_RET,          FETCH_OP_GDS_USHORT_READ_RET     },
   {DS_OP_ATOMIC_ORDERED_ALLOC_RET, FETCH_OP_GDS_ATOMIC_ORDERED_ALLOC},
   {DS_OP_INVALID,                  0                                },
};

} // namespace r600
