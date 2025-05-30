/*
 * Copyright (c) 2017 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "util/ralloc.h"

#include "ppir.h"

ppir_instr *ppir_instr_create(ppir_block *block)
{
   ppir_instr *instr = rzalloc(block, ppir_instr);
   if (!instr)
      return NULL;

   list_inithead(&instr->succ_list);
   list_inithead(&instr->pred_list);

   instr->index = block->comp->cur_instr_index++;
   instr->reg_pressure = -1;

   list_addtail(&instr->list, &block->instr_list);
   return instr;
}

void ppir_instr_add_dep(ppir_instr *succ, ppir_instr *pred)
{
   /* don't add duplicated instr */
   ppir_instr_foreach_pred(succ, dep) {
      if (pred == dep->pred)
         return;
   }

   ppir_dep *dep = ralloc(succ, ppir_dep);
   dep->pred = pred;
   dep->succ = succ;
   list_addtail(&dep->pred_link, &succ->pred_list);
   list_addtail(&dep->succ_link, &pred->succ_list);
}

void ppir_instr_insert_mul_node(ppir_node *add, ppir_node *mul)
{
   ppir_instr *instr = add->instr;
   int pos = mul->instr_pos;
   int *slots = ppir_op_infos[mul->op].slots;

   for (int i = 0; slots[i] != PPIR_INSTR_SLOT_END; i++) {
      /* possible to insert at required place */
      if (slots[i] == pos) {
         if (!instr->slots[pos]) {
            ppir_alu_node *add_alu = ppir_node_to_alu(add);
            ppir_alu_node *mul_alu = ppir_node_to_alu(mul);
            ppir_dest *dest = &mul_alu->dest;
            int pipeline = pos == PPIR_INSTR_SLOT_ALU_VEC_MUL ?
               ppir_pipeline_reg_vmul : ppir_pipeline_reg_fmul;

            /* ^vmul/^fmul can't be used as last arg */
            if (add_alu->num_src > 1) {
               ppir_src *last_src = add_alu->src + add_alu->num_src - 1;
               if (ppir_node_target_equal(last_src, dest))
                  return;
            }

            /* update add node src to use pipeline reg */
            ppir_src *src = add_alu->src;
            if (add_alu->num_src == 3) {
               if (ppir_node_target_equal(src, dest)) {
                  src->type = ppir_target_pipeline;
                  src->pipeline = pipeline;
               } else
                  /* Do not pipeline select sources if select condition is not
                   * pipelined yet */
                  return;

               if (ppir_node_target_equal(++src, dest)) {
                  src->type = ppir_target_pipeline;
                  src->pipeline = pipeline;
               }
            }
            else {
               assert(ppir_node_target_equal(src, dest));
               src->type = ppir_target_pipeline;
               src->pipeline = pipeline;
            }

            /* update mul node dest to output to pipeline reg */
            dest->type = ppir_target_pipeline;
            dest->pipeline = pipeline;

            instr->slots[pos] = mul;
            mul->instr = instr;
         }
         return;
      }
   }
}

/* check whether a const slot fix into another const slot */
static bool ppir_instr_insert_const(ppir_const *dst, const ppir_const *src,
                                    uint8_t *swizzle)
{
   int i, j;

   for (i = 0; i < src->num; i++) {
      for (j = 0; j < dst->num; j++) {
         if (src->value[i].ui == dst->value[j].ui)
            break;
      }

      if (j == dst->num) {
         if (dst->num == 4)
            return false;
         dst->value[dst->num++] = src->value[i];
      }

      swizzle[i] = j;
   }

   return true;
}

static void ppir_update_src_pipeline(ppir_pipeline pipeline, ppir_src *src,
                                     ppir_dest *dest, uint8_t *swizzle)
{
   if (ppir_node_target_equal(src, dest)) {
      src->type = ppir_target_pipeline;
      src->pipeline = pipeline;

      if (swizzle) {
         for (int k = 0; k < 4; k++)
            src->swizzle[k] = swizzle[src->swizzle[k]];
      }
   }
}

/* make alu node src reflact the pipeline reg */
static void ppir_instr_update_src_pipeline(ppir_instr *instr, ppir_pipeline pipeline,
                                           ppir_dest *dest, uint8_t *swizzle)
{
   for (int i = PPIR_INSTR_SLOT_ALU_START; i <= PPIR_INSTR_SLOT_ALU_END; i++) {
      if (!instr->slots[i])
         continue;

      ppir_alu_node *alu = ppir_node_to_alu(instr->slots[i]);
      for (int j = 0; j < alu->num_src; j++) {
         ppir_src *src = alu->src + j;
         ppir_update_src_pipeline(pipeline, src, dest, swizzle);
      }
   }

   ppir_node *branch_node = instr->slots[PPIR_INSTR_SLOT_BRANCH];
   if (branch_node && (branch_node->type == ppir_node_type_branch)) {
      ppir_branch_node *branch = ppir_node_to_branch(branch_node);
      for (int j = 0; j < 2; j++) {
         ppir_src *src = branch->src + j;
         ppir_update_src_pipeline(pipeline, src, dest, swizzle);
      }
   }
}

bool ppir_instr_insert_node(ppir_instr *instr, ppir_node *node)
{
   if (node->op == ppir_op_const) {
      int i;
      ppir_const_node *c = ppir_node_to_const(node);
      const ppir_const *nc = &c->constant;

      for (i = 0; i < 2; i++) {
         ppir_const ic = instr->constant[i];
         uint8_t swizzle[4] = {0};

         if (ppir_instr_insert_const(&ic, nc, swizzle)) {
            instr->constant[i] = ic;
            ppir_node *succ = ppir_node_first_succ(node);
            for (int s = 0; s < ppir_node_get_src_num(succ); s++) {
               ppir_src *src = ppir_node_get_src(succ, s);
               assert(src);
               if (src->node != node)
                  continue;

               ppir_update_src_pipeline(ppir_pipeline_reg_const0 + i, src,
                                        &c->dest, swizzle);
            }
            break;
         }
      }

      /* no const slot can insert */
      if (i == 2)
         return false;

      return true;
   }
   else {
      int *slots = ppir_op_infos[node->op].slots;
      for (int i = 0; slots[i] != PPIR_INSTR_SLOT_END; i++) {
         int pos = slots[i];
         ppir_dest *dest = ppir_node_get_dest(node);

         if (instr->slots[pos]) {
            /* node already in this instr, i.e. load_uniform */
            if (instr->slots[pos] == node)
               return true;
            else {
               if (pos == PPIR_INSTR_SLOT_UNIFORM && node->op == ppir_op_load_uniform) {
                  ppir_load_node *load = ppir_node_to_load(node);
                  ppir_load_node *existing = ppir_node_to_load(instr->slots[PPIR_INSTR_SLOT_UNIFORM]);

                  if (!load->num_src && !existing->num_src &&
                      load->index == existing->index &&
                      load->num_components == existing->num_components) {
                     ppir_debug("Re-using uniform slot of instr %d with node %d for node %d\n",
                                 instr->index, node->index, existing->node.index);
                     node->instr = instr;
                     return true;
                  }
               }
               continue;
            }
         }

         if (pos == PPIR_INSTR_SLOT_BRANCH) {
            /* Branch and combiner run in parallel, they cannot be inserted
             * into the same instruction */
            if (instr->slots[PPIR_INSTR_SLOT_ALU_COMBINE])
               return false;
         }

         if (pos == PPIR_INSTR_SLOT_ALU_VEC_MUL) {
            if (dest && dest->type == ppir_target_pipeline) {
               ppir_node *add = ppir_node_first_succ(node);
               if (add->instr_pos == PPIR_INSTR_SLOT_ALU_SCL_ADD)
                  continue;
            }
         }

         if (pos == PPIR_INSTR_SLOT_ALU_SCL_MUL) {
            if (dest && dest->type == ppir_target_pipeline) {
               ppir_node *succ = ppir_node_first_succ(node);
               if (succ->instr_pos == PPIR_INSTR_SLOT_ALU_VEC_ADD && succ->op != ppir_op_select)
                  continue;
            }

            /* Do not schedule into scalar mul slot if there is a select in any add
             * slot */
            ppir_node *sadd = instr->slots[PPIR_INSTR_SLOT_ALU_SCL_ADD];
            ppir_node *vadd = instr->slots[PPIR_INSTR_SLOT_ALU_VEC_ADD];
            if (dest && dest->type != ppir_target_pipeline &&
                ((sadd && sadd->op == ppir_op_select) || (vadd && vadd->op == ppir_op_select)))
               continue;
         }

         /* Don't attempt to schedule a node with pipeline reg destination
          * into slots that cannot have a pipeline output */
         switch (pos)
         {
         case PPIR_INSTR_SLOT_ALU_SCL_ADD:
         case PPIR_INSTR_SLOT_ALU_VEC_ADD:
         case PPIR_INSTR_SLOT_ALU_COMBINE:
            if (dest && dest->type == ppir_target_pipeline)
               continue;
            break;
         default:
            break;
         }

         /* Don't schedule add node if it has a pipelined predecessor, but
          * corresponding slot is already taken */
         if (pos == PPIR_INSTR_SLOT_ALU_VEC_ADD || pos == PPIR_INSTR_SLOT_ALU_SCL_ADD) {
            int mul_pos = pos == PPIR_INSTR_SLOT_ALU_VEC_ADD ?
                           PPIR_INSTR_SLOT_ALU_VEC_MUL :
                           PPIR_INSTR_SLOT_ALU_SCL_ADD;
            for (int i = 0; i < ppir_node_get_src_num(node); i++) {
               ppir_src *src = ppir_node_get_src(node, i);
               if (src->type == ppir_target_pipeline &&
                   (src->pipeline == ppir_pipeline_reg_fmul ||
                    src->pipeline == ppir_pipeline_reg_vmul))
                  if (instr->slots[mul_pos])
                     continue;
            }
         }

         /* Don't schedule select into add node if smul slot is already taken
          */
         if (node->op == ppir_op_select && instr->slots[PPIR_INSTR_SLOT_ALU_SCL_MUL])
            break;

         /* Don't schedule to mul slot if it outputs to pipeline reg,
          * but corresponding ADD slot is taken */
         if (pos == PPIR_INSTR_SLOT_ALU_VEC_MUL || pos == PPIR_INSTR_SLOT_ALU_SCL_MUL) {
            ppir_node *add;
            ppir_pipeline pipeline;
            if (pos == PPIR_INSTR_SLOT_ALU_VEC_MUL) {
               add = instr->slots[PPIR_INSTR_SLOT_ALU_VEC_ADD];
               pipeline = ppir_pipeline_reg_vmul;
            } else {
               add = instr->slots[PPIR_INSTR_SLOT_ALU_SCL_ADD];
               pipeline = ppir_pipeline_reg_fmul;
            }
            if (add) {
               for (int i = 0; i < ppir_node_get_src_num(add); i++) {
                  ppir_src *src = ppir_node_get_src(add, i);
                  if (src->type == ppir_target_pipeline && src->pipeline == pipeline &&
                      src->node != node)
                     continue;
               }
            }
         }

         /* Handle select condition for vector mul */
         if (pos == PPIR_INSTR_SLOT_ALU_SCL_MUL) {
            ppir_node *select = instr->slots[PPIR_INSTR_SLOT_ALU_VEC_ADD];
            if (select && select->op == ppir_op_select) {
               ppir_src *src = ppir_node_get_src(select, 0);
               if (src->node != node)
                  continue;
            }
         }

         if (pos == PPIR_INSTR_SLOT_ALU_COMBINE) {
            /* Branch unit runs in parallel with combiner, so we cannot
             * schedule into combiner slot if branch slot is used
             */
            if (instr->slots[PPIR_INSTR_SLOT_BRANCH])
               continue;

            if (!ppir_target_is_scalar(dest))
               continue;
            /* Combiner doesn't have pipeline destination */
            if (dest->type == ppir_target_pipeline)
               continue;

            /* No modifiers on vector output */
            if (node->op == ppir_op_mul && dest->modifier != ppir_outmod_none)
               continue;

            /* No modifiers on vector source on combiner */
            if (ppir_node_get_src_num(node) == 2) {
               ppir_src *src = ppir_node_get_src(node, 1);
               if (src->negate || src->absolute)
                  continue;
            }
         }

         if (pos == PPIR_INSTR_SLOT_ALU_SCL_MUL ||
             pos == PPIR_INSTR_SLOT_ALU_SCL_ADD) {
            if (!ppir_target_is_scalar(dest))
               continue;
         }

         instr->slots[pos] = node;
         node->instr = instr;
         node->instr_pos = pos;

         if ((node->op == ppir_op_load_uniform) || (node->op == ppir_op_load_temp)) {
            ppir_load_node *l = ppir_node_to_load(node);
            ppir_instr_update_src_pipeline(
               instr, ppir_pipeline_reg_uniform, &l->dest, NULL);
         }

         /* Scalar add can be placed in vector slot, update pipeline source
          * register if it is the case
          */
         if (pos == PPIR_INSTR_SLOT_ALU_VEC_ADD) {
            if (ppir_target_is_scalar(dest)) {
               for (int i = 0; i < ppir_node_get_src_num(node); i++)
               {
                  /* Don't update condition on select, it always comes from
                   * fmul */
                  if (i == 0 && node->op == ppir_op_select)
                     continue;
                  ppir_src *src = ppir_node_get_src(node, i);
                  if (src->type == ppir_target_pipeline && src->pipeline == ppir_pipeline_reg_fmul)
                     src->pipeline = ppir_pipeline_reg_vmul;
               }
            }
         }

         /* Scalar mul can be placed in vector slot, update pipeline dest
          * register if it is the case
          */
         if (pos == PPIR_INSTR_SLOT_ALU_VEC_MUL) {
            if (dest->type == ppir_target_pipeline)
               dest->pipeline = ppir_pipeline_reg_vmul;
         }

         return true;
      }

      return false;
   }
}

static struct {
   int len;
   char *name;
} ppir_instr_fields[] = {
   [PPIR_INSTR_SLOT_VARYING] = { 4, "vary" },
   [PPIR_INSTR_SLOT_TEXLD] = { 4, "texl"},
   [PPIR_INSTR_SLOT_UNIFORM] = { 4, "unif" },
   [PPIR_INSTR_SLOT_ALU_VEC_MUL] = { 4, "vmul" },
   [PPIR_INSTR_SLOT_ALU_SCL_MUL] = { 4, "smul" },
   [PPIR_INSTR_SLOT_ALU_VEC_ADD] = { 4, "vadd" },
   [PPIR_INSTR_SLOT_ALU_SCL_ADD] = { 4, "sadd" },
   [PPIR_INSTR_SLOT_ALU_COMBINE] = { 4, "comb" },
   [PPIR_INSTR_SLOT_STORE_TEMP] = { 4, "stor" },
   [PPIR_INSTR_SLOT_BRANCH] = { 4, "brch" },
};

void ppir_instr_print_list(ppir_compiler *comp)
{
   if (!(lima_debug & LIMA_DEBUG_PP))
      return;

   printf("======ppir instr list======\n");
   printf("      ");
   for (int i = 0; i < PPIR_INSTR_SLOT_NUM; i++)
      printf("%-*s ", ppir_instr_fields[i].len, ppir_instr_fields[i].name);
   printf("const0|1\n");

   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      printf("-------block %3d-------\n", block->index);
      list_for_each_entry(ppir_instr, instr, &block->instr_list, list) {
         printf("%c%03d: ", instr->stop ? '*' : ' ', instr->index);
         for (int i = 0; i < PPIR_INSTR_SLOT_NUM; i++) {
            ppir_node *node = instr->slots[i];
            if (node)
               printf("%-*d ", ppir_instr_fields[i].len, node->index);
            else
               printf("%-*s ", ppir_instr_fields[i].len, "null");
         }
         for (int i = 0; i < 2; i++) {
            if (i)
               printf("| ");

            for (int j = 0; j < instr->constant[i].num; j++)
               printf("%f ", instr->constant[i].value[j].f);
         }
         printf("\n");
      }
   }
   printf("===========================\n");
}

static void ppir_instr_print_sub(ppir_instr *instr)
{
   printf("[%s%d",
          instr->printed && !ppir_instr_is_leaf(instr) ? "+" : "",
          instr->index);

   if (!instr->printed) {
      ppir_instr_foreach_pred(instr, dep) {
         ppir_instr_print_sub(dep->pred);
      }

      instr->printed = true;
   }

   printf("]");
}

void ppir_instr_print_dep(ppir_compiler *comp)
{
   if (!(lima_debug & LIMA_DEBUG_PP))
      return;

   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_instr, instr, &block->instr_list, list) {
         instr->printed = false;
      }
   }

   printf("======ppir instr depend======\n");
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      printf("-------block %3d-------\n", block->index);
      list_for_each_entry(ppir_instr, instr, &block->instr_list, list) {
         if (ppir_instr_is_root(instr)) {
            ppir_instr_print_sub(instr);
            printf("\n");
         }
      }
   }
   printf("=============================\n");
}
