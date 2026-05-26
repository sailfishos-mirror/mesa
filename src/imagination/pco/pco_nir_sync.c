/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir_sync.c
 *
 * \brief PCO NIR sync-related passes.
 */

#include "hwdef/rogue_hw_defs.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_builtin_builder.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "pco_usclib.h"
#include "util/macros.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * \brief Lowers a barrier instruction.
 *
 * \param[in] b NIR builder.
 * \param[in] intr NIR intrinsic instruction.
 * \param[in] cb_data User callback data.
 * \return True if progress was made.
 */
static bool
lower_barrier(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_barrier)
      return false;

   struct shader_info *info = &b->shader->info;
   bool *uses_usclib = cb_data;

   mesa_scope exec_scope = nir_intrinsic_execution_scope(intr);

   unsigned wg_size = info->workgroup_size[0] * info->workgroup_size[1] *
                      info->workgroup_size[2];

   if (wg_size <= ROGUE_MAX_INSTANCES_PER_TASK || exec_scope == SCOPE_NONE ||
       exec_scope == SCOPE_SUBGROUP) {
      nir_instr_remove(&intr->instr);
      return true;
   }

   /* TODO: We might be able to re-use barrier counters. */
   unsigned counter_offset = info->shared_size;
   info->shared_size += sizeof(uint32_t);
   info->zero_initialize_shared_memory = true;

   *uses_usclib = true;

   unsigned num_slots = DIV_ROUND_UP(wg_size, ROGUE_MAX_INSTANCES_PER_TASK);

   b->cursor = nir_before_instr(&intr->instr);
   usclib_barrier(b, nir_imm_int(b, num_slots), nir_imm_int(b, counter_offset));
   nir_instr_remove(&intr->instr);
   return true;
}

/**
 * \brief Barrier lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_barriers(nir_shader *shader, pco_data *data)
{
   bool progress = nir_shader_intrinsics_pass(shader,
                                              lower_barrier,
                                              nir_metadata_none,
                                              &data->common.uses.usclib);

   data->common.uses.barriers |= progress;

   return progress;
}

static bool
lower_usclib_atomic(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_ssbo_atomic_swap &&
       intr->intrinsic != nir_intrinsic_global_atomic_swap_pco)
      return false;

   bool *uses_usclib = cb_data;

   b->cursor = nir_before_instr(&intr->instr);

   if (intr->intrinsic == nir_intrinsic_ssbo_atomic_swap) {
      nir_def *buffer = intr->src[0].ssa;
      nir_def *offset = intr->src[1].ssa;
      nir_def *value = intr->src[2].ssa;
      nir_def *value_swap = intr->src[3].ssa;

      ASSERTED enum gl_access_qualifier access = nir_intrinsic_access(intr);
      ASSERTED unsigned num_components = intr->def.num_components;
      ASSERTED unsigned bit_size = intr->def.bit_size;
      assert(access == ACCESS_COHERENT);
      assert(num_components == 1 && bit_size == 32);

      *uses_usclib = true;
      nir_def *emulated =
         usclib_emu_ssbo_atomic_comp_swap(b, buffer, offset, value, value_swap);
      nir_def_rewrite_uses(&intr->def, emulated);
      nir_instr_remove(&intr->instr);
      return true;
   }

   nir_def *addr_data = intr->src[0].ssa;
   nir_def *addr = nir_channels(b, addr_data, BITFIELD_RANGE(0, 2));
   nir_def *value = nir_channel(b, addr_data, 2);
   nir_def *value_swap = nir_channel(b, addr_data, 3);

   ASSERTED unsigned num_components = intr->def.num_components;
   ASSERTED unsigned bit_size = intr->def.bit_size;
   assert(num_components == 1 && bit_size == 32);

   *uses_usclib = true;

   nir_def *emulated =
      usclib_emu_global_atomic_comp_swap(b, addr, value, value_swap);
   nir_def_rewrite_uses(&intr->def, emulated);
   nir_instr_remove(&intr->instr);
   return true;
}

static bool lower_global_atomic_intrinsic(nir_builder *b,
                                          nir_intrinsic_instr *intrin,
                                          UNUSED void *data)
{
   nir_intrinsic_op op;
   bool is_swap = false;
   switch (intrin->intrinsic) {
   case nir_intrinsic_global_atomic_2x32:
      op = nir_intrinsic_global_atomic_pco;
      break;
   case nir_intrinsic_global_atomic_swap_2x32:
      op = nir_intrinsic_global_atomic_swap_pco;
      is_swap = true;
      break;
   default:
      return false;
   }

   nir_src *addr_src = &intrin->src[0];
   nir_src *data_src = &intrin->src[1];
   nir_src *data2_src;
   if (is_swap)
      data2_src = &intrin->src[2];

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *new_src = nir_pad_vector(b, addr_src->ssa, is_swap ? 4 : 3);
   new_src = nir_vector_insert_imm(b, new_src, data_src->ssa, 2);
   if (is_swap)
      new_src = nir_vector_insert_imm(b, new_src, data2_src->ssa, 3);

   nir_intrinsic_instr *new_intrin = nir_intrinsic_instr_create(b->shader, op);
   new_intrin->num_components = intrin->num_components;
   nir_def_init(&new_intrin->instr,
                &new_intrin->def,
                intrin->def.num_components,
                intrin->def.bit_size);
   new_intrin->src[0] = nir_src_for_ssa(new_src);
   assert(nir_intrinsic_has_atomic_op(intrin));
   nir_intrinsic_set_atomic_op(new_intrin, nir_intrinsic_atomic_op(intrin));

   nir_builder_instr_insert(b, &new_intrin->instr);
   nir_def_rewrite_uses(&intrin->def, &new_intrin->def);
   nir_instr_remove(&intrin->instr);

   return true;
}

/**
 * \brief Atomics lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_atomics(nir_shader *shader, pco_data *data)
{
   bool progress = false;

   progress |= nir_shader_intrinsics_pass(shader,
                                          lower_global_atomic_intrinsic,
                                          nir_metadata_control_flow,
                                          NULL);
   progress |= nir_shader_intrinsics_pass(shader,
                                          lower_usclib_atomic,
                                          nir_metadata_none,
                                          &data->common.uses.usclib);

   return progress;
}

struct subgroup_state {
   nir_variable *per_subgroup_var;
};

static nir_variable *
per_subgroup_var(nir_shader *shader, struct subgroup_state *state)
{
   struct shader_info *info = &shader->info;

   /* Allocate a 32-bit var for each subgroup. */
   if (!state->per_subgroup_var) {
      unsigned num_subgroups =
         DIV_ROUND_UP(info->workgroup_size[0] *
                      info->workgroup_size[1] *
                      info->workgroup_size[2],
                      ROGUE_MAX_INSTANCES_PER_TASK);

      const glsl_type *var_type =
         glsl_array_type(glsl_uint_type(), num_subgroups, 0);

      state->per_subgroup_var = nir_variable_create(shader,
                                                    nir_var_mem_shared,
                                                    var_type,
                                                    "per_subgroup_var");
   }

   return state->per_subgroup_var;
}

static nir_def *
lower_read_invocation(nir_builder *b,
                      nir_intrinsic_instr *intr,
                      struct subgroup_state *state)
{
   nir_def *subgroup_id =
      nir_udiv_imm(b,
                   nir_load_local_invocation_index(b),
                   ROGUE_MAX_INSTANCES_PER_TASK);

   nir_def *value = intr->src[0].ssa;
   nir_def *invoc = intr->src[1].ssa;

   nir_variable *var = per_subgroup_var(b->shader, state);
   nir_deref_instr *deref = nir_build_deref_var(b, var);
   deref = nir_build_deref_array(b, deref, subgroup_id);

   nir_def *invocation_id = nir_load_instance_num_pco(b);

   nir_if *nif = nir_push_if(b, nir_ieq(b, invoc, invocation_id));
   {
      nir_store_deref(b, deref, value, 1);
   }
   nir_pop_if(b, nif);

   /* Retrieve the value. */
   return nir_load_deref(b, deref);
}

static inline nir_def *elect(nir_builder *b)
{
   return nir_ieq(b, nir_load_instance_num_pco(b), nir_first_invocation(b));
}

static nir_def *
lower_ballot(nir_builder *b,
             nir_intrinsic_instr *intr,
             struct subgroup_state *state)
{
   nir_def *subgroup_id =
      nir_udiv_imm(b,
                   nir_load_local_invocation_index(b),
                   ROGUE_MAX_INSTANCES_PER_TASK);

   nir_variable *var = per_subgroup_var(b->shader, state);
   nir_deref_instr *deref = nir_build_deref_var(b, var);
   deref = nir_build_deref_array(b, deref, subgroup_id);

   /* Initialize the ballot shared var to zero. */
   nir_if *nif = nir_push_if(b, elect(b));
   {
      nir_store_deref(b, deref, nir_imm_int(b, 0), 1);
   }
   nir_pop_if(b, nif);

   /* Each mask = value << invocation_num */
   nir_def *ballot_mask =
      nir_ishl(b, nir_b2i32(b, intr->src[0].ssa), nir_load_instance_num_pco(b));

   /* OR all the masks together. */
   nir_deref_atomic(b,
                    intr->def.bit_size,
                    &deref->def,
                    ballot_mask,
                    .atomic_op = nir_atomic_op_ior);

   /* Retrieve the value. */
   return nir_load_deref(b, deref);
}

static bool lower_subgroup_intrinsic(nir_builder *b,
                                     nir_intrinsic_instr *intr,
                                     void *cb_data)
{
   const struct shader_info *info = &b->shader->info;
   struct subgroup_state *state = cb_data;
   nir_def *new_def;

   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_subgroup_size:
      new_def = nir_imm_int(b, info->api_subgroup_size);
      break;

   case nir_intrinsic_load_num_subgroups:
      new_def = nir_imm_int(b,
                            DIV_ROUND_UP(info->workgroup_size[0] *
                                            info->workgroup_size[1] *
                                            info->workgroup_size[2],
                                         ROGUE_MAX_INSTANCES_PER_TASK));
      break;

   case nir_intrinsic_load_subgroup_invocation:
      new_def = nir_load_instance_num_pco(b);
      break;

   case nir_intrinsic_load_subgroup_id:
      new_def = nir_udiv_imm(b,
                             nir_load_local_invocation_index(b),
                             ROGUE_MAX_INSTANCES_PER_TASK);
      break;

   case nir_intrinsic_read_invocation:
      new_def = lower_read_invocation(b, intr, state);
      break;

   case nir_intrinsic_ballot:
   case nir_intrinsic_ballot_relaxed:
      new_def = lower_ballot(b, intr, state);
      break;

   default:
      return false;
   }

   nir_def_rewrite_uses(&intr->def, new_def);
   nir_instr_remove(&intr->instr);
   assert(intr->def.num_components == 1);
   return true;
}

bool pco_nir_lower_subgroups(nir_shader *shader)
{
   if (shader->info.internal)
      return false;

   assert(shader->info.api_subgroup_size == ROGUE_MAX_INSTANCES_PER_TASK);

   struct subgroup_state state = { 0 };
   return nir_shader_intrinsics_pass(shader,
                                     lower_subgroup_intrinsic,
                                     nir_metadata_none,
                                     &state);
}
