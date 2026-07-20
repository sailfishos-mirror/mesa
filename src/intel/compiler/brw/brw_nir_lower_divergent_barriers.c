/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_nir.h"

struct active_thread_lowering_state {
   nir_function_impl *impl;

   /** Tracks the number of barriers issued. */
   nir_variable *num_barriers_var;

   /** Tracks the number of active threads. */
   nir_variable *slm_atomic_vars;

   /** If there are multiple subgroups. */
   nir_def *is_multiple_subgroups;

   /** If the SLM writes in the prologue are synced. */
   bool is_prologue_synced;
};

static void
workgroup_barrier(nir_builder *b)
{
   nir_barrier(b,
               .execution_scope = SCOPE_WORKGROUP,
               .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL,
               .memory_modes = nir_var_mem_shared);
}

static bool
loop_has_divergent_halt(nir_cf_node *first_node)
{
   if (!exec_node_is_head_sentinel(first_node->node.prev) ||
       first_node->parent->type != nir_cf_node_loop)
      return false;

   nir_foreach_block_in_cf_node(loop_block, first_node->parent) {
      if (loop_block->divergent &&
          nir_block_ends_in_return_or_halt(loop_block)) {
         return true;
      }
   }

   return false;
}

static bool
lower_active_thread_barriers_counters(struct active_thread_lowering_state *state)
{
   /* We need to do some extra work to track divergence from halt
    * instructions because they are ignored by divergence analysis.
    */
   bool divergent_halt = false;

   nir_foreach_block_safe(block, state->impl) {
      if (!divergent_halt && loop_has_divergent_halt(&block->cf_node))
         divergent_halt = true;

      if (!block->divergent && !divergent_halt && state->is_prologue_synced)
         continue;

      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_barrier ||
             nir_intrinsic_execution_scope(intrin) != SCOPE_WORKGROUP)
            continue;

         if (!block->divergent && !divergent_halt) {
            state->is_prologue_synced = true;
            nir_intrinsic_set_memory_modes(intrin,
               nir_intrinsic_memory_modes(intrin) | nir_var_mem_shared);
            continue;
         }

         if (!state->num_barriers_var) {
            state->num_barriers_var =
               nir_local_variable_create(state->impl, glsl_uint_type(),
                                         "brw_num_barriers");
         }

         nir_builder b = nir_builder_at(nir_before_instr(instr));
         nir_def *invocation = nir_load_subgroup_invocation(&b);

         /* Only increment on a single lane per subgroup so the reduction at
          * the end of the program counts each barrier exactly once.
          */
         nir_push_if(&b, nir_ieq(&b, invocation, nir_first_invocation(&b)));
         {
            nir_def *value = nir_load_var(&b, state->num_barriers_var);
            value = nir_iadd_imm(&b, value, 1);
            nir_store_var(&b, state->num_barriers_var, value, 0x1);
         }
         nir_pop_if(&b, NULL);
      }

      if (!divergent_halt && block->divergent)
         divergent_halt = nir_block_ends_in_return_or_halt(block);
   }

   return state->num_barriers_var;
}

static void
insert_active_thread_barriers_prologue(struct active_thread_lowering_state *state)
{
   nir_shader *nir = state->impl->function->shader;
   unsigned wg_size = nir_static_workgroup_size(nir);
   nir_builder b = nir_builder_at(nir_before_impl(state->impl));

   state->slm_atomic_vars =
      nir_variable_create(nir, nir_var_mem_shared,
                          glsl_array_type(glsl_uint_type(), 2, 4),
                          "brw_barrier_sync");
   state->slm_atomic_vars->data.how_declared = nir_var_hidden;

   /* Set this up so DCE removes the prologue/epilogue if there's one subgroup. */
   state->is_multiple_subgroups =
      wg_size <= nir->info.max_subgroup_size ?
         nir_ilt_imm(&b, nir_load_simd_width_intel(&b), wg_size) :
         nir_imm_true(&b);

   nir_push_if(&b, state->is_multiple_subgroups);
   {
      nir_push_if(&b, nir_ieq_imm(&b, nir_load_local_invocation_index(&b), 0));
      {
         for (unsigned phase = 0; phase <= 1; ++phase) {
            nir_store_array_var_imm(&b, state->slm_atomic_vars, phase,
                                    nir_load_num_subgroups(&b), 0x1);
         }
      }
      nir_pop_if(&b, NULL);
   }
   nir_pop_if(&b, NULL);

   nir_store_var(&b, state->num_barriers_var, nir_imm_int(&b, 0), 0x1);
}

static void
insert_active_thread_barriers_epilogue(struct active_thread_lowering_state *state)
{
   /* Make sure nothing jumps past the last block */
   if (nir_lower_halt_to_return(state->impl->function->shader)) {
      nir_progress(true, state->impl, nir_metadata_none);
      nir_lower_returns_impl(state->impl);
   }

   nir_builder b = nir_builder_at(nir_after_impl(state->impl));

   nir_push_if(&b, state->is_multiple_subgroups);
   {
      nir_def *total_barriers =
         nir_reduce(&b, nir_load_var(&b, state->num_barriers_var),
                    .reduction_op = nir_op_iadd);

      /* Make sure the writes in the prologue have landed. */
      if (!state->is_prologue_synced) {
         nir_push_if(&b, nir_ieq_imm(&b, total_barriers, 0));
         {
            workgroup_barrier(&b);
         }
         nir_pop_if(&b, NULL);
      }

      nir_deref_instr *counter_vars[2];

      for (unsigned phase = 0; phase <= 1; ++phase) {
         /* Select the counters in SLM to sync with, we alternate between two
          * locations depending on if we're at an even or odd barrier to avoid
          * a WaR hazard while checking the result.
          */
         counter_vars[phase] =
            nir_build_deref_array(&b,
                                 nir_build_deref_var(&b, state->slm_atomic_vars),
                                 nir_ixor(&b,
                                          nir_iand_imm(&b, total_barriers, 1),
                                          nir_imm_int(&b, phase)));

         /* Decrement the SLM counter. */
         nir_push_if(&b, nir_ieq(&b, nir_load_subgroup_invocation(&b),
                                 nir_first_invocation(&b)));
         {
            nir_atomic_deref(&b, 32, counter_vars[phase], nir_imm_int(&b, -1),
                             nir_atomic_op_iadd);
         }
         nir_pop_if(&b, NULL);

         workgroup_barrier(&b);

         /* Halt if the counter has reached zero. */
         nir_def *cnt = nir_load_deref(&b, counter_vars[phase]);
         nir_halt_if(&b, nir_ieq_imm(&b, cnt, 0));
      }

      /* Keep issuing barriers until the counter reaches zero. */
      nir_push_loop(&b);
      {
         for (unsigned phase = 0; phase <= 1; ++phase) {
            workgroup_barrier(&b);

            nir_def *cnt = nir_load_deref(&b, counter_vars[phase]);
            nir_halt_if(&b, nir_ieq_imm(&b, cnt, 0));
         }
      }
      nir_pop_loop(&b, NULL);
   }
   nir_pop_if(&b, NULL);
}

/**
 * Emulates Xe2+ active thread barriers on Gfx125 and below. This is comparable
 * to IGC's DivergentBarrierPass, except its more compact and efficient.
 */
bool
brw_nir_lower_active_thread_barriers(nir_shader *nir,
                                     const struct intel_device_info *devinfo)
{
   assert(exec_list_length(&nir->functions) == 1);

   struct active_thread_lowering_state state = {
      .impl = nir_shader_get_entrypoint(nir),
   };

   if (devinfo->ver >= 20)
      return false;

   if (!nir->info.uses_control_barrier)
      return false;

   if (nir_static_workgroup_size(nir) <= nir->info.min_subgroup_size)
      return false;

   nir_divergence_analysis_impl(state.impl, nir_divergence_across_subgroups);

   if (!lower_active_thread_barriers_counters(&state))
      return false;

   insert_active_thread_barriers_prologue(&state);
   insert_active_thread_barriers_epilogue(&state);

   return nir_progress(true, state.impl, nir_metadata_none);
}
