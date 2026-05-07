/*
 * Copyright 2009 Nicolai Hähnle <nhaehnle@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include "r300_tgsi_to_rc.h"

#include "compiler/nir_to_rc.h"
#include "compiler/radeon_compiler.h"

#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_util.h"

#include "util/compiler.h"

static int translate_register_index(
    struct tgsi_to_rc * ttr,
    unsigned file,
    int index)
{
    if (file == TGSI_FILE_IMMEDIATE)
        return ttr->immediate_offset + index;

    return index;
}

static void transform_dstreg(
    struct tgsi_to_rc * ttr,
    struct rc_dst_register * dst,
    struct tgsi_full_dst_register * src)
{
    dst->File = rc_translate_register_file(src->Register.File);
    dst->Index = translate_register_index(ttr, src->Register.File, src->Register.Index);
    dst->WriteMask = src->Register.WriteMask;

    if (src->Register.Indirect) {
        ttr->error = true;
        fprintf(stderr, "r300: Relative addressing of destination operands "
                "is unsupported.\n");
    }
}

static void transform_srcreg(
    struct tgsi_to_rc * ttr,
    struct rc_src_register * dst,
    struct tgsi_full_src_register * src)
{
    dst->File = rc_translate_register_file(src->Register.File);
    int index = translate_register_index(ttr, src->Register.File, src->Register.Index);
    /* Negative offsets to relative addressing should have been lowered in NIR */
    assert(index >= 0);
    /* Also check for overflow */
    if (index >= RC_REGISTER_MAX_INDEX) {
        ttr->error = true;
        fprintf(stderr, "r300: Register index too high.\n");
    }
    dst->Index = index;
    dst->RelAddr = src->Register.Indirect;
    dst->Swizzle = tgsi_util_get_full_src_register_swizzle(src, 0);
    dst->Swizzle |= tgsi_util_get_full_src_register_swizzle(src, 1) << 3;
    dst->Swizzle |= tgsi_util_get_full_src_register_swizzle(src, 2) << 6;
    dst->Swizzle |= tgsi_util_get_full_src_register_swizzle(src, 3) << 9;
    dst->Abs = src->Register.Absolute;
    dst->Negate = src->Register.Negate ? RC_MASK_XYZW : 0;
}

static void transform_texture(struct rc_instruction * dst, struct tgsi_instruction_texture src)
{
    dst->U.I.TexSrcTarget = rc_translate_tex_target(src.Texture);
    dst->U.I.TexSwizzle = RC_SWIZZLE_XYZW;
}

static void transform_instruction(struct tgsi_to_rc * ttr, struct tgsi_full_instruction * src)
{
    struct rc_instruction * dst;
    int i;

    dst = rc_insert_new_instruction(ttr->compiler, ttr->compiler->Program.Instructions.Prev);
    dst->U.I.Opcode = rc_translate_opcode(src->Instruction.Opcode);
    dst->U.I.SaturateMode = rc_translate_saturate(src->Instruction.Saturate);

    if (src->Instruction.NumDstRegs)
        transform_dstreg(ttr, &dst->U.I.DstReg, &src->Dst[0]);

    for(i = 0; i < src->Instruction.NumSrcRegs; ++i) {
        if (src->Src[i].Register.File == TGSI_FILE_SAMPLER)
            dst->U.I.TexSrcUnit = src->Src[i].Register.Index;
        else
            transform_srcreg(ttr, &dst->U.I.SrcReg[i], &src->Src[i]);
    }

    /* Texturing. */
    if (src->Instruction.Texture)
        transform_texture(dst, src->Texture);
}

static void handle_immediate(struct tgsi_to_rc * ttr,
                             struct tgsi_full_immediate * imm,
                             unsigned index)
{
    struct rc_constant constant;

    constant.Type = RC_CONSTANT_IMMEDIATE;
    constant.UseMask = RC_MASK_XYZW;
    for (unsigned i = 0; i < 4; ++i)
        constant.u.Immediate[i] = imm->u[i].Float;
    rc_constants_add(&ttr->compiler->Program.Constants, &constant);
}

void r300_tgsi_to_rc(struct tgsi_to_rc * ttr,
                     const struct tgsi_token * tokens)
{
    struct tgsi_full_instruction *inst;
    struct tgsi_parse_context parser;
    unsigned imm_index = 0;
    int i;

    ttr->error = false;

    /* Allocate constants placeholders.
     *
     * Note: What if declared constants are not contiguous? */
    unsigned num_constants = 0;
    nir_foreach_variable_with_modes (var, ttr->shader, nir_var_mem_ubo) {
        assert(num_constants == 0);
        unsigned size = glsl_get_explicit_size(var->interface_type, false);
        num_constants = DIV_ROUND_UP(size, 16);
    }
    for(i = 0; i < num_constants; ++i) {
        struct rc_constant constant;
        memset(&constant, 0, sizeof(constant));
        constant.Type = RC_CONSTANT_EXTERNAL;
        constant.UseMask = RC_MASK_XYZW;
        constant.u.External = i;
        rc_constants_add(&ttr->compiler->Program.Constants, &constant);
    }

    ttr->immediate_offset = ttr->compiler->Program.Constants.Count;

    tgsi_parse_init(&parser, tokens);

    while (!tgsi_parse_end_of_tokens(&parser)) {
        tgsi_parse_token(&parser);

        switch (parser.FullToken.Token.Type) {
            case TGSI_TOKEN_TYPE_DECLARATION:
                break;
            case TGSI_TOKEN_TYPE_IMMEDIATE:
                handle_immediate(ttr, &parser.FullToken.FullImmediate, imm_index);
                imm_index++;
                break;
            case TGSI_TOKEN_TYPE_INSTRUCTION:
                inst = &parser.FullToken.FullInstruction;
                if (inst->Instruction.Opcode == TGSI_OPCODE_END) {
                    break;
                }

                transform_instruction(ttr, inst);
                break;
        }
    }

    tgsi_parse_free(&parser);

    rc_calculate_inputs_outputs(ttr->compiler);
}
