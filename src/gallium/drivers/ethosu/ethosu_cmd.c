/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include "util/macros.h"
#include "util/u_dynarray.h"

#include "ethosu_cmd.h"
#include "ethosu_coefs.h"
#include "ethosu_ml.h"
#include "ethosu_registers.h"
#include "ethosu_sched.h"

#define MAX_BLOCKDEP            3
#define MAX_OUTSTANDING_DMA_OPS 2
#define MAX_OUTSTANDING_NPU_OPS 2

enum ethosu_op_to_scale {
   OP_NONE = 0,
   OP_A = 1,
   OP_B = 2,
};

static void
ethosu_ensure_cmdstream(struct ethosu_subgraph *subgraph)
{
   if ((subgraph->cursor - subgraph->cmdstream) < (subgraph->cmdstream_used - 2))
      return;

   unsigned cur_size = subgraph->cursor - subgraph->cmdstream;
   subgraph->cmdstream = realloc(subgraph->cmdstream, (subgraph->cmdstream_used + 32) * sizeof(*subgraph->cmdstream));
   subgraph->cursor = subgraph->cmdstream + cur_size;
   subgraph->cmdstream_used += 32;
}

#define EMIT0(cmd, param)                                                                      \
   do {                                                                                        \
      ethosu_ensure_cmdstream(subgraph);                                                       \
      *(subgraph->cursor++) = cmd | (((param) & 0xFFFF) << 16);                                \
      if (DBG_ENABLED(ETHOSU_DBG_MSGS))                                                        \
         fprintf(stderr, "emit0(%s, 0x%x);\n", ethosu_get_cmd_name(0, cmd), (param) & 0xFFFF); \
   } while (0)

#define EMIT1(cmd, param, offset)                                                                                   \
   do {                                                                                                             \
      ethosu_ensure_cmdstream(subgraph);                                                                            \
      *(subgraph->cursor++) = cmd | 0x4000 | (((param) & 0xFFFF) << 16);                                            \
      *(subgraph->cursor++) = (offset) & 0xFFFFFFFF;                                                                \
      if (DBG_ENABLED(ETHOSU_DBG_MSGS))                                                                             \
         fprintf(stderr, "emit1(%s, 0x%x, 0x%x);\n", ethosu_get_cmd_name(1, cmd), (param) & 0xFFFF, (int)(offset)); \
   } while (0)

static void
emit_addresses(
   struct ethosu_subgraph *subgraph,
   struct ethosu_feature_map *feature_map,
   uint32_t cmd_base0, uint32_t cmd_base1, uint32_t cmd_base2, uint32_t cmd_base3)
{
   EMIT1(cmd_base0, 0x0, feature_map->tiles.addresses[0]);
   EMIT1(cmd_base1, 0x0, feature_map->tiles.addresses[1]);
   EMIT1(cmd_base2, 0x0, feature_map->tiles.addresses[2]);
   EMIT1(cmd_base3, 0x0, feature_map->tiles.addresses[3]);
}

static void
emit_tiles(
   struct ethosu_subgraph *subgraph,
   struct ethosu_feature_map *feature_map,
   uint32_t cmd_height0, uint32_t cmd_height1, uint32_t cmd_width0)
{
   EMIT0(cmd_height0, feature_map->tiles.height_0 - 1);
   EMIT0(cmd_height1, feature_map->tiles.height_1 - 1);
   EMIT0(cmd_width0, feature_map->tiles.width_0 - 1);
}

static void
emit_strides(
   struct ethosu_subgraph *subgraph,
   struct ethosu_feature_map *feature_map,
   uint32_t cmd_stride_c, uint32_t cmd_stride_y, uint32_t cmd_stride_x)
{
   unsigned elem_size = 1;
   unsigned tensor_x, tensor_y, tensor_c;
   struct ethosu_tensor *tensor = ethosu_find_tensor(subgraph, feature_map->tensor_idx);

   if (tensor->layout == ETHOSU_LAYOUT_NHCWB16) {
      tensor_x = 16 * elem_size;
      tensor_c = tensor_x * tensor->shape.width;
      tensor_y = elem_size * tensor->shape.width * align(tensor->shape.depth, 16);
   } else {
      tensor_c = elem_size;
      tensor_x = tensor->shape.depth * tensor_c;
      tensor_y = tensor->shape.width * tensor_x;
   }

   EMIT1(cmd_stride_c, 0x0, tensor_c);
   EMIT1(cmd_stride_y, 0x0, tensor_y);
   EMIT1(cmd_stride_x, 0x0, tensor_x);
}

static void
emit_ifm(struct ethosu_subgraph *subgraph, struct ethosu_feature_map *feature_map)
{
   EMIT0(NPU_SET_IFM_REGION, IO_REGION);
   emit_addresses(
      subgraph,
      feature_map,
      NPU_SET_IFM_BASE0,
      NPU_SET_IFM_BASE1,
      NPU_SET_IFM_BASE2,
      NPU_SET_IFM_BASE3);

   emit_tiles(
      subgraph, feature_map, NPU_SET_IFM_HEIGHT0_M1, NPU_SET_IFM_HEIGHT1_M1, NPU_SET_IFM_WIDTH0_M1);

   EMIT0(NPU_SET_IFM_DEPTH_M1, feature_map->shape.depth - 1);
   emit_strides(subgraph, feature_map, NPU_SET_IFM_STRIDE_C, NPU_SET_IFM_STRIDE_Y, NPU_SET_IFM_STRIDE_X);
   EMIT0(NPU_SET_IFM_ZERO_POINT, feature_map->zero_point);
}

static void
emit_ifm_precision(struct ethosu_subgraph *subgraph,
                   struct ethosu_feature_map *feature_map,
                   enum ethosu_op_to_scale op_to_scale, uint32_t precision_cmd)
{
   struct ethosu_tensor *tensor = ethosu_find_tensor(subgraph, feature_map->tensor_idx);
   unsigned prec = 0;

   if (tensor->layout == ETHOSU_LAYOUT_NHCWB16)
      prec |= NPU_SET_IFM_PRECISION_FORMAT(1);

   prec |= NPU_SET_IFM_PRECISION_PRECISION(feature_map->precision);

   if (feature_map->is_signed)
      prec |= NPU_SET_IFM_PRECISION_ACTIVATION(1); // signed activation

   prec |= NPU_SET_IFM_PRECISION_SCALE_MODE(op_to_scale);

   EMIT0(precision_cmd, prec);
}

static void
emit_padding(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_IFM_PAD_TOP, operation->pad.top);
   EMIT0(NPU_SET_IFM_PAD_LEFT, operation->pad.left);
   EMIT0(NPU_SET_IFM_PAD_BOTTOM, operation->pad.bottom);
   EMIT0(NPU_SET_IFM_PAD_RIGHT, operation->pad.right);
}

static void
emit_ofm(struct ethosu_subgraph *subgraph, struct ethosu_feature_map *feature_map)
{
   EMIT0(NPU_SET_OFM_REGION, IO_REGION);
   emit_addresses(
      subgraph,
      feature_map,
      NPU_SET_OFM_BASE0,
      NPU_SET_OFM_BASE1,
      NPU_SET_OFM_BASE2,
      NPU_SET_OFM_BASE3);
   emit_tiles(
      subgraph, feature_map, NPU_SET_OFM_HEIGHT0_M1, NPU_SET_OFM_HEIGHT1_M1, NPU_SET_OFM_WIDTH0_M1);
   EMIT0(NPU_SET_OFM_HEIGHT_M1, feature_map->shape.height - 1);
   EMIT0(NPU_SET_OFM_WIDTH_M1, feature_map->shape.width - 1);
   EMIT0(NPU_SET_OFM_DEPTH_M1, feature_map->shape.depth - 1);
   emit_strides(subgraph, feature_map, NPU_SET_OFM_STRIDE_C, NPU_SET_OFM_STRIDE_Y, NPU_SET_OFM_STRIDE_X);
   EMIT0(NPU_SET_OFM_ZERO_POINT, feature_map->zero_point);
}

static void
emit_ofm_precision(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   struct ethosu_tensor *tensor = ethosu_find_tensor(subgraph, operation->ofm.tensor_idx);
   unsigned prec = 0;

   if (tensor->layout == ETHOSU_LAYOUT_NHCWB16)
      prec |= NPU_SET_OFM_PRECISION_FORMAT(1);

   prec |= NPU_SET_OFM_PRECISION_PRECISION(operation->ofm.precision);

   if (operation->ofm.is_signed)
      prec |= NPU_SET_OFM_PRECISION_ACTIVATION(1);

   if (operation->type == ETHOSU_OPERATION_TYPE_POOLING ||
       operation->type == ETHOSU_OPERATION_TYPE_ELTWISE) {
      prec |= NPU_SET_OFM_PRECISION_SCALE_MODE(1);
   }

   prec |= NPU_SET_OFM_PRECISION_ROUND_MODE(operation->round_mode);

   EMIT0(NPU_SET_OFM_PRECISION, prec);
}

static void
emit_kernel(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_KERNEL_HEIGHT_M1, operation->kernel.height - 1);
   EMIT0(NPU_SET_KERNEL_WIDTH_M1, operation->kernel.width - 1);
   unsigned stride = (operation->kernel.stride_x - 1) & 1;
   stride |= ((operation->kernel.stride_y - 1) & 1) << 1;
   stride |= ((operation->kernel.stride_x - 1) >> 1) << 6;
   stride |= ((operation->kernel.stride_y - 1) >> 1) << 9;
   stride |= (operation->kernel.dilation_x - 1) << 3;
   stride |= (operation->kernel.dilation_y - 1) << 4;
   stride |= operation->conv.part_kernel_first << 2;
   EMIT0(NPU_SET_KERNEL_STRIDE, stride);
}

static void
emit_weights(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_WEIGHT_REGION, operation->conv.weights.region);
   EMIT1(NPU_SET_WEIGHT_BASE, 0x0, operation->conv.weights.address);
   EMIT1(NPU_SET_WEIGHT_LENGTH, 0x0, operation->conv.weights.size);
}

static void
emit_biases(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_SCALE_REGION, operation->conv.scales.region);
   EMIT1(NPU_SET_SCALE_BASE, 0x0, operation->conv.scales.address);
   EMIT1(NPU_SET_SCALE_LENGTH, 0x0, operation->conv.scales.size);
}

static void
emit_activation(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   unsigned min = 0;

   if (operation->type == ETHOSU_OPERATION_TYPE_ELTWISE)
      min = operation->eltwise.activation_min;

   EMIT0(NPU_SET_ACTIVATION, 0x0);

   if (operation->ofm.is_signed) {
      EMIT0(NPU_SET_ACTIVATION_MIN, 0xff80);
      EMIT0(NPU_SET_ACTIVATION_MAX, 0x7f);
   } else {
      EMIT0(NPU_SET_ACTIVATION_MIN, min);
      EMIT0(NPU_SET_ACTIVATION_MAX, 0xff);
   }
}

static void
emit_block_config(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_OFM_BLK_HEIGHT_M1, operation->block_config.ofm_block.height - 1);
   EMIT0(NPU_SET_OFM_BLK_WIDTH_M1, operation->block_config.ofm_block.width - 1);
   EMIT0(NPU_SET_OFM_BLK_DEPTH_M1, operation->block_config.ofm_block.depth - 1);
}

static void
emit_shram_registers(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_IFM_IB_END, operation->block_config.shram_layout.ib_end);
   EMIT0(NPU_SET_AB_START, operation->block_config.shram_layout.ab_start);

   if (operation->type == ETHOSU_OPERATION_TYPE_ELTWISE)
      EMIT0(NPU_SET_IFM2_IB_START, operation->block_config.shram_layout.ib_start2);

   EMIT0(NPU_SET_ACC_FORMAT, operation->block_config.acc_type);
}

static void
emit_common(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, enum ethosu_op_to_scale op_to_scale)
{
   emit_ifm(subgraph, &operation->ifm);
   emit_ifm_precision(subgraph, &operation->ifm, op_to_scale, NPU_SET_IFM_PRECISION);
   EMIT0(NPU_SET_IFM_UPSCALE, operation->upscale);

   if (operation->type != ETHOSU_OPERATION_TYPE_ELTWISE)
      emit_padding(subgraph, operation);

   emit_ofm(subgraph, &operation->ofm);

   emit_ofm_precision(subgraph, operation);

   if (operation->type != ETHOSU_OPERATION_TYPE_ELTWISE)
      emit_kernel(subgraph, operation);

   if (operation->type == ETHOSU_OPERATION_TYPE_CONVOLUTION) {
      emit_weights(subgraph, operation);
      emit_biases(subgraph, operation);
   }

   emit_activation(subgraph, operation);

   emit_block_config(subgraph, operation);
   if (ethosu_is_u65(ethosu_screen(subgraph->base.context->screen)))
      emit_shram_registers(subgraph, operation);
   else
      EMIT0(NPU_SET_ACC_FORMAT, 0x300); // FIXME should be based on # of MACs, only works for >=256 MACs
}

static void
emit_convolution(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   ethosu_allocate_feature_map(subgraph, &operation->ifm);
   operation->ifm.tiles.height_0 = operation->ifm.shape.height;
   operation->ifm.tiles.height_1 = operation->ifm.shape.height;
   operation->ifm.tiles.width_0 = operation->ifm.shape.width;

   ethosu_allocate_feature_map(subgraph, &operation->ofm);
   operation->ofm.tiles.height_0 = operation->ofm.shape.height;
   operation->ofm.tiles.height_1 = operation->ofm.shape.height;
   operation->ofm.tiles.width_0 = operation->ofm.shape.width;

   emit_common(subgraph, operation, false);
}

static unsigned
quantise_pooling_scale(unsigned nr_kernel_elements, unsigned rescale_bits, unsigned *out_shift)
{
   int k = 0;
   long long N = 0;

   frexp(nr_kernel_elements - 1, &k);
   N = 31 - rescale_bits;
   *out_shift = N + k;

   return ((1LL << (N + k)) + (1LL << k)) / nr_kernel_elements;
}

static unsigned
pooling_emit_ofm_scaling(
   double input1_scale,
   double output_scale,
   unsigned kernel_height,
   unsigned kernel_width,
   uint32_t *out_shift)
{
   double rescale = input1_scale / output_scale;
   unsigned rescale_bits = 0;
   unsigned scale;

   if (kernel_height == 1 && kernel_width == 1) {
      if (rescale > 1.0)
         rescale_bits = 32 - __builtin_clz(ceil(rescale)) + 1;
      else if (rescale < 1.0)
         rescale_bits = -(32 - __builtin_clz(ceil(1 / rescale))) - 1;
   }
   scale = quantise_pooling_scale(kernel_height * kernel_width, rescale_bits, out_shift);
   scale = ceil(scale * rescale);
   return scale;
}

static void
emit_pooling(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   unsigned scale;
   unsigned scale_shift;

   emit_common(subgraph, operation, false);

   if (operation->pooling.avg) {
      scale = pooling_emit_ofm_scaling(
         operation->ifm.scale,
         operation->ofm.scale,
         operation->kernel.height,
         operation->kernel.width,
         &scale_shift);

      EMIT1(NPU_SET_OFM_SCALE, scale_shift, scale);
   }
}

static void
emit_ifm2(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, bool has_scalar)
{
   if (!has_scalar) {
      EMIT0(NPU_SET_IFM2_REGION, IO_REGION);
      emit_addresses(subgraph, &operation->ifm2, NPU_SET_IFM2_BASE0, NPU_SET_IFM2_BASE1, NPU_SET_IFM2_BASE2, NPU_SET_IFM2_BASE3);
      emit_tiles(subgraph, &operation->ifm2, NPU_SET_IFM2_HEIGHT0_M1, NPU_SET_IFM2_HEIGHT1_M1, NPU_SET_IFM2_WIDTH0_M1);
      emit_strides(subgraph, &operation->ifm2, NPU_SET_IFM2_STRIDE_C, NPU_SET_IFM2_STRIDE_Y, NPU_SET_IFM2_STRIDE_X);
   } else {
      EMIT0(NPU_SET_IFM2_SCALAR, operation->ifm2.scalar);
   }
   EMIT0(NPU_SET_IFM2_ZERO_POINT, operation->ifm2.zero_point);
}

static void
emit_ifm2_broadcast(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, bool has_scalar)
{
   unsigned ifm2_broadcast = 0;

   ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_OPERAND_ORDER(operation->eltwise.ifm_reversed);

   if (has_scalar) {
      ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_BROADCAST_SCALAR(1);
   } else {
      if (operation->ifm.shape.height != operation->ifm2.shape.height)
         ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_BROADCAST_HEIGHT__MASK;
      if (operation->ifm.shape.width != operation->ifm2.shape.width)
         ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_BROADCAST_WIDTH__MASK;
      if (operation->ifm.shape.depth != operation->ifm2.shape.depth)
         ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_BROADCAST_DEPTH__MASK;
   }

   EMIT0(NPU_SET_IFM2_BROADCAST, ifm2_broadcast);
}

/*
 * Elementwise ADD/SUB scaling from Vela advanced_elementwise_add_sub_scale().
 * Scale up the operand with smaller scale to match the larger one.
 * OPA_SCALE has the input scaling, OPB_SCALE is 0.
 * op_to_scale in IFM_PRECISION tells NPU which operand to scale.
 */
static enum ethosu_op_to_scale
advanced_elementwise_add_sub_scale(
   struct ethosu_subgraph *subgraph,
   double input1_scale,
   double input2_scale,
   double output_scale)
{
   double max_input_scale = MAX2(input1_scale, input2_scale);
   double min_input_scale = MIN2(input1_scale, input2_scale);
   unsigned bitdepth = 8;
   uint32_t input_shift = (bitdepth == 8) ? 20 : 15;
   double input_shift_val = (double)(1ULL << input_shift);
   enum ethosu_op_to_scale op_to_scale;
   uint32_t opa_scale, opa_shift;
   uint32_t ofm_scale, ofm_shift;
   double input_rescale, output_rescale;

   /* Scale the operand with smaller scale */
   if (input1_scale < input2_scale)
      op_to_scale = OP_A;
   else
      op_to_scale = OP_B;

   /* From Vela simplified_elementwise_add_sub_scale:
    * input1_rescale = input1_scale * (1 << input_shift) / (2 * max_input_scale)
    * output_rescale = (2 * max_input_scale) / (output_scale * (1 << input_shift))
    */
   input_rescale = min_input_scale * input_shift_val / (2.0 * max_input_scale);
   output_rescale = (2.0 * max_input_scale) / (output_scale * input_shift_val);

   opa_scale = ethosu_quantize_scale(input_rescale, &opa_shift);
   ofm_scale = ethosu_quantize_scale(output_rescale, &ofm_shift);

   EMIT1(NPU_SET_OPA_SCALE, opa_shift, opa_scale);
   EMIT1(NPU_SET_OPB_SCALE, 0x0, 0x0);
   EMIT1(NPU_SET_OFM_SCALE, ofm_shift, ofm_scale);

   return op_to_scale;
}

static void
emit_eltwise(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   bool has_scalar = operation->ifm2.scalar != 0;
   enum ethosu_op_to_scale op_to_scale;

   op_to_scale = advanced_elementwise_add_sub_scale(
      subgraph,
      operation->ifm.scale,
      operation->ifm2.scale,
      operation->ofm.scale);

   if (operation->eltwise.ifm_reversed) {
      if (op_to_scale == OP_A)
         op_to_scale = OP_B;
      else
         op_to_scale = OP_A;
   }

   emit_common(subgraph, operation, op_to_scale);

   emit_ifm2(subgraph, operation, has_scalar);
   emit_ifm_precision(subgraph, &operation->ifm2, OP_NONE, NPU_SET_IFM2_PRECISION);
   emit_ifm2_broadcast(subgraph, operation, has_scalar);
}

static void
emit_dma(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_DMA0_SRC_REGION, COEFS_REGION);
   EMIT1(NPU_SET_DMA0_SRC, 0x0, operation->dma.address);
   EMIT0(NPU_SET_DMA0_DST_REGION, SCRATCH_REGION);
   EMIT1(NPU_SET_DMA0_DST, 0x0, 0x0);
   EMIT1(NPU_SET_DMA0_LEN, 0x0, operation->dma.size);
}

static void
emit_operation_code(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   switch (operation->type) {
   case ETHOSU_OPERATION_TYPE_CONVOLUTION:

      if (operation->conv.depthwise)
         EMIT0(NPU_OP_DEPTHWISE, 0x0);
      else
         EMIT0(NPU_OP_CONV, 0x0);

      break;
   case ETHOSU_OPERATION_TYPE_POOLING:
      EMIT0(NPU_OP_POOL, operation->pooling.avg);
      break;
   case ETHOSU_OPERATION_TYPE_ELTWISE:
      EMIT0(NPU_OP_ELEMENTWISE, 0x1);
      break;
   case ETHOSU_OPERATION_TYPE_DMA:
      EMIT0(NPU_OP_DMA_START, 0x0);
      break;
   }
}

static void
emit_cmd_waits(struct ethosu_subgraph *subgraph, int npu_waits, int dma_waits)
{
   if (npu_waits >= 0)
      EMIT0(NPU_OP_KERNEL_WAIT, npu_waits);

   if (dma_waits >= 0)
      EMIT0(NPU_OP_DMA_WAIT, dma_waits);
}

static bool
ethosu_intersects_accesses(struct ethosu_address_range *a, struct ethosu_address_range *b)
{
   for (int i = 0; i < MAX_MEMORY_ACCESSES; i++) {
      for (int j = 0; j < MAX_MEMORY_ACCESSES; j++) {
         if (a[i].size == 0 || b[j].size == 0)
            continue;
         if (a[i].region != b[j].region)
            continue;
         if (a[i].address < b[j].address + b[j].size &&
             b[j].address < a[i].address + a[i].size)
            return true;
      }
   }

   return false;
}

static bool
ethosu_operations_conflict(struct ethosu_subgraph *subgraph,
                           struct ethosu_operation *op1, struct ethosu_operation *op2)
{
   /* True dependencies, or write -> read */
   if (ethosu_intersects_accesses(op1->write_accesses, op2->read_accesses))
      return true;

   /* Anti-dependencies, or read -> write */
   if (ethosu_intersects_accesses(op1->read_accesses, op2->write_accesses))
      return true;

   /* Output dependencies, or write -> write */
   if (ethosu_intersects_accesses(op1->write_accesses, op2->write_accesses))
      return true;

   /* read -> read does not cause a conflict */
   return false;
}

static void
get_wait_dependency(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation,
                    struct util_dynarray *outstanding_dma_ops,
                    struct util_dynarray *outstanding_npu_ops,
                    int *npu_waits, int *dma_waits)
{
   unsigned kern_wait = -1;
   unsigned dma_wait = -1;
   struct util_dynarray *outstanding_ops = NULL;

   if (operation->type == ETHOSU_OPERATION_TYPE_DMA) {
      outstanding_ops = outstanding_npu_ops;

      util_dynarray_append(outstanding_dma_ops, operation);

      unsigned dmap_ops = util_dynarray_num_elements(outstanding_dma_ops, struct ethosu_operation *);
      if (dmap_ops > MAX_OUTSTANDING_DMA_OPS)
         (void)util_dynarray_pop(outstanding_dma_ops, struct ethosu_operation *);
   } else {
      outstanding_ops = outstanding_dma_ops;

      util_dynarray_append(outstanding_npu_ops, operation);

      unsigned npu_ops = util_dynarray_num_elements(outstanding_npu_ops, struct ethosu_operation *);
      if (npu_ops > MAX_OUTSTANDING_NPU_OPS)
         (void)util_dynarray_pop(outstanding_npu_ops, struct ethosu_operation *);
   }

   unsigned waits = -1;
   for (int idx = util_dynarray_num_elements(outstanding_ops, struct ethosu_operation *) - 1; idx >= 0; idx--) {
      waits += 1;
      struct ethosu_operation *other_op = *util_dynarray_element(outstanding_ops, struct ethosu_operation *, idx);
      if (other_op == operation)
         continue;
      if (ethosu_operations_conflict(subgraph, other_op, operation)) {
         if (operation->type == ETHOSU_OPERATION_TYPE_DMA)
            kern_wait = waits;
         else
            dma_wait = waits;
         // Current op needs to wait, and after it has waited,
         // outstanding_ops[0..idx] are not outstanding any longer.
         for (int i = 0; i <= idx; i++)
            (void)util_dynarray_pop(outstanding_ops, struct ethosu_operation *);
         break;
      }
   }

   *npu_waits = kern_wait;
   *dma_waits = dma_wait;
}

static void
fill_memory_accesses(struct ethosu_subgraph *subgraph)
{
   util_dynarray_foreach (&subgraph->operations, struct ethosu_operation, operation) {
      switch (operation->type) {
      case ETHOSU_OPERATION_TYPE_DMA:
         operation->read_accesses[0].region = COEFS_REGION;
         operation->read_accesses[0].address = operation->dma.address;
         operation->read_accesses[0].size = operation->dma.size;

         operation->write_accesses[0].region = SCRATCH_REGION;
         operation->write_accesses[0].address = 0x0;
         operation->write_accesses[0].size = operation->dma.size;

         break;
      default:
         operation->read_accesses[0].region = IO_REGION;
         operation->read_accesses[0].address = operation->ifm.tiles.addresses[0];
         operation->read_accesses[0].size = operation->ifm.shape.height * operation->ifm.shape.width * operation->ifm.shape.depth;

         operation->read_accesses[1].region = IO_REGION;
         operation->read_accesses[1].address = operation->ifm2.tiles.addresses[0];
         operation->read_accesses[1].size = operation->ifm2.shape.height * operation->ifm2.shape.width * operation->ifm2.shape.depth;

         operation->read_accesses[2].region = operation->conv.scales.region;
         operation->read_accesses[2].address = operation->conv.scales.address;
         operation->read_accesses[2].size = operation->conv.scales.size;

         operation->read_accesses[3].region = operation->conv.weights.region;
         operation->read_accesses[3].address = operation->conv.weights.address;
         operation->read_accesses[3].size = operation->conv.weights.size;

         operation->write_accesses[0].region = IO_REGION;
         operation->write_accesses[0].address = operation->ofm.tiles.addresses[0];
         operation->write_accesses[0].size = operation->ofm.shape.height * operation->ofm.shape.width * operation->ofm.shape.depth;
         break;
      }
   }
}

static bool
fm_ranges_overlap(struct ethosu_subgraph *subgraph,
                  struct ethosu_feature_map *a, struct ethosu_feature_map *b)
{
   struct ethosu_tensor *ta = ethosu_find_tensor(subgraph, a->tensor_idx);
   struct ethosu_tensor *tb = ethosu_find_tensor(subgraph, b->tensor_idx);

   if (!ta || !tb || ta->size == 0 || tb->size == 0)
      return false;

   return ta->offset < tb->offset + tb->size &&
          tb->offset < ta->offset + ta->size;
}

static unsigned
calc_blockdep(struct ethosu_subgraph *subgraph, struct ethosu_operation *prev_op, struct ethosu_operation *operation)
{
   if (!prev_op)
      return 0;

   // Check if the reserved shram will be used in current/prev op
   bool prev_uses_lut = false; // prev_op->activation && prev_op->activation->op_type == NpuActivationOp.TABLE_LOOKUP;
   bool curr_uses_lut = false; // operation->activation && operation->activation->op_type == NpuActivationOp.TABLE_LOOKUP;
   if (prev_uses_lut && SHRAM_RESERVED_UNUSED_BANKS == 0 && !curr_uses_lut)
      return 0;

   /* If the previous op writes to the same buffer that the current op
    * reads from, we need to wait for it to finish first.
    */
   bool ifm_overlaps = fm_ranges_overlap(subgraph, &prev_op->ofm, &operation->ifm);
   bool ifm2_overlaps = operation->type == ETHOSU_OPERATION_TYPE_ELTWISE &&
                         fm_ranges_overlap(subgraph, &prev_op->ofm, &operation->ifm2);

   if (ifm_overlaps || ifm2_overlaps)
      return 0;

   return MAX_BLOCKDEP;
}

void
ethosu_emit_cmdstream(struct ethosu_subgraph *subgraph)
{
   struct ethosu_operation *prev_op = NULL;
   struct util_dynarray outstanding_dma_ops;
   struct util_dynarray outstanding_npu_ops;

   outstanding_dma_ops = UTIL_DYNARRAY_INIT;
   outstanding_npu_ops = UTIL_DYNARRAY_INIT;

   subgraph->cmdstream_used = 32;
   subgraph->cmdstream = calloc(subgraph->cmdstream_used, sizeof(*subgraph->cmdstream));
   subgraph->cursor = subgraph->cmdstream;

   fill_memory_accesses(subgraph);

   /* Compile */

   if (ethosu_is_u65(ethosu_screen(subgraph->base.context->screen)))
      EMIT0(NPU_SET_PARALLEL_MODE, 0x0);

   util_dynarray_foreach (&subgraph->operations, struct ethosu_operation, operation) {

      int npu_waits, dma_waits;

      get_wait_dependency(subgraph, operation, &outstanding_dma_ops, &outstanding_npu_ops,
                          &npu_waits, &dma_waits);

      switch (operation->type) {
      case ETHOSU_OPERATION_TYPE_CONVOLUTION:
         emit_convolution(subgraph, operation);
         break;
      case ETHOSU_OPERATION_TYPE_POOLING:
         emit_pooling(subgraph, operation);
         break;
      case ETHOSU_OPERATION_TYPE_ELTWISE:
         emit_eltwise(subgraph, operation);
         break;
      case ETHOSU_OPERATION_TYPE_DMA:
         emit_dma(subgraph, operation);
         break;
      }

      if (operation->type != ETHOSU_OPERATION_TYPE_DMA) {
         unsigned blockdep = calc_blockdep(subgraph, prev_op, operation);
         blockdep = MIN2(blockdep, MAX_BLOCKDEP);
         EMIT0(NPU_SET_BLOCKDEP, blockdep);

         prev_op = operation;
      }

      emit_cmd_waits(subgraph, npu_waits, dma_waits);
      emit_operation_code(subgraph, operation);
   }

   EMIT0(NPU_OP_STOP, 0xffff);

   util_dynarray_fini(&outstanding_dma_ops);
   util_dynarray_fini(&outstanding_npu_ops);
}
