/*
 * Copyright © 2014 Broadcom
 * SPDX-License-Identifier: MIT
 */

#ifndef NIR_TO_RC_H
#define NIR_TO_RC_H

#include <stdbool.h>
#include "compiler/nir/nir.h"
#include "pipe/p_defines.h"

struct nir_shader;
struct pipe_screen;
struct r300_fragment_program_external_state;

const void *nir_to_rc(struct nir_shader *s, struct pipe_screen *screen,
                      struct r300_fragment_program_external_state state);

void
ntr_fixup_varying_slots(struct nir_shader *s, nir_variable_mode mode);

#endif /* NIR_TO_RC_H */
