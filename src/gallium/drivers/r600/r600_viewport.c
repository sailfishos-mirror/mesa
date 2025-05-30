/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "r600_cs.h"
#include "util/u_viewport.h"
#include "tgsi/tgsi_scan.h"
#include "r600d.h"

#define R600_R_028C0C_PA_CL_GB_VERT_CLIP_ADJ         0x028C0C
#define CM_R_028BE8_PA_CL_GB_VERT_CLIP_ADJ           0x28be8
#define R_02843C_PA_CL_VPORT_XSCALE                  0x02843C

#define R_028250_PA_SC_VPORT_SCISSOR_0_TL                               0x028250
#define   S_028250_TL_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028250_TL_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028250_TL_X                                               0xFFFF8000
#define   S_028250_TL_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028250_TL_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028250_TL_Y                                               0x8000FFFF
#define   S_028250_WINDOW_OFFSET_DISABLE(x)                           (((unsigned)(x) & 0x1) << 31)
#define   G_028250_WINDOW_OFFSET_DISABLE(x)                           (((x) >> 31) & 0x1)
#define   C_028250_WINDOW_OFFSET_DISABLE                              0x7FFFFFFF
#define   S_028254_BR_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028254_BR_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028254_BR_X                                               0xFFFF8000
#define   S_028254_BR_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028254_BR_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028254_BR_Y                                               0x8000FFFF
#define R_0282D0_PA_SC_VPORT_ZMIN_0                                     0x0282D0
#define R_0282D4_PA_SC_VPORT_ZMAX_0                                     0x0282D4

#define GET_MAX_SCISSOR(rctx) (rctx->gfx_level >= EVERGREEN ? 16384 : 8192)

static void r600_set_scissor_states(struct pipe_context *ctx,
				    unsigned start_slot,
				    unsigned num_scissors,
				    const struct pipe_scissor_state *state)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	int i;

	for (i = 0; i < num_scissors; i++)
		rctx->scissors.states[start_slot + i] = state[i];

	if (!rctx->scissor_enabled)
		return;

	rctx->scissors.dirty_mask |= ((1 << num_scissors) - 1) << start_slot;
	rctx->set_atom_dirty(rctx, &rctx->scissors.atom, true);
}

/* Since the guard band disables clipping, we have to clip per-pixel
 * using a scissor.
 */
static void r600_get_scissor_from_viewport(struct r600_common_context *rctx,
					   const struct pipe_viewport_state *vp,
					   struct r600_signed_scissor *scissor)
{
	float tmp, minx, miny, maxx, maxy;

	/* Convert (-1, -1) and (1, 1) from clip space into window space. */
	minx = -vp->scale[0] + vp->translate[0];
	miny = -vp->scale[1] + vp->translate[1];
	maxx = vp->scale[0] + vp->translate[0];
	maxy = vp->scale[1] + vp->translate[1];

	/* r600_draw_rectangle sets this. Disable the scissor. */
	if (minx == -1 && miny == -1 && maxx == 1 && maxy == 1) {
		scissor->minx = scissor->miny = 0;
		scissor->maxx = scissor->maxy = GET_MAX_SCISSOR(rctx);
		return;
	}

	/* Handle inverted viewports. */
	if (minx > maxx) {
		tmp = minx;
		minx = maxx;
		maxx = tmp;
	}
	if (miny > maxy) {
		tmp = miny;
		miny = maxy;
		maxy = tmp;
	}

	/* Convert to integer and round up the max bounds. */
	scissor->minx = minx;
	scissor->miny = miny;
	scissor->maxx = ceilf(maxx);
	scissor->maxy = ceilf(maxy);
}

static void r600_clamp_scissor(struct r600_common_context *rctx,
			       struct pipe_scissor_state *out,
			       struct r600_signed_scissor *scissor)
{
	unsigned max_scissor = GET_MAX_SCISSOR(rctx);
	out->minx = CLAMP(scissor->minx, 0, max_scissor);
	out->miny = CLAMP(scissor->miny, 0, max_scissor);
	out->maxx = CLAMP(scissor->maxx, 0, max_scissor);
	out->maxy = CLAMP(scissor->maxy, 0, max_scissor);
}

static void r600_clip_scissor(struct pipe_scissor_state *out,
			      struct pipe_scissor_state *clip)
{
	out->minx = MAX2(out->minx, clip->minx);
	out->miny = MAX2(out->miny, clip->miny);
	out->maxx = MIN2(out->maxx, clip->maxx);
	out->maxy = MIN2(out->maxy, clip->maxy);
}

static void r600_scissor_make_union(struct r600_signed_scissor *out,
				    struct r600_signed_scissor *in)
{
	out->minx = MIN2(out->minx, in->minx);
	out->miny = MIN2(out->miny, in->miny);
	out->maxx = MAX2(out->maxx, in->maxx);
	out->maxy = MAX2(out->maxy, in->maxy);
}

void evergreen_apply_scissor_bug_workaround(struct r600_common_context *rctx,
					    struct pipe_scissor_state *scissor)
{
	if (rctx->gfx_level == EVERGREEN || rctx->gfx_level == CAYMAN) {
		if (scissor->maxx == 0)
			scissor->minx = 1;
		if (scissor->maxy == 0)
			scissor->miny = 1;

		if (rctx->gfx_level == CAYMAN &&
		    scissor->maxx == 1 && scissor->maxy == 1)
			scissor->maxx = 2;
	}
}

static void r600_emit_one_scissor(struct r600_common_context *rctx,
				  struct radeon_cmdbuf *cs,
				  struct r600_signed_scissor *vp_scissor,
				  struct pipe_scissor_state *scissor)
{
	struct pipe_scissor_state final;

	if (rctx->vs_disables_clipping_viewport) {
		final.minx = final.miny = 0;
		final.maxx = final.maxy = GET_MAX_SCISSOR(rctx);
	} else {
		r600_clamp_scissor(rctx, &final, vp_scissor);
	}

	if (scissor)
		r600_clip_scissor(&final, scissor);

	evergreen_apply_scissor_bug_workaround(rctx, &final);

	radeon_emit(cs, S_028250_TL_X(final.minx) |
			S_028250_TL_Y(final.miny) |
			S_028250_WINDOW_OFFSET_DISABLE(1));
	radeon_emit(cs, S_028254_BR_X(final.maxx) |
			S_028254_BR_Y(final.maxy));
}

/* the range is [-MAX, MAX] */
#define GET_MAX_VIEWPORT_RANGE(rctx) (rctx->gfx_level >= EVERGREEN ? 32768 : 16384)

static void r600_emit_guardband(struct r600_common_context *rctx,
				struct r600_signed_scissor *vp_as_scissor)
{
	struct radeon_cmdbuf *cs = &rctx->gfx.cs;
	struct pipe_viewport_state vp;
	float left, top, right, bottom, max_range, guardband_x, guardband_y;

	/* Reconstruct the viewport transformation from the scissor. */
	vp.translate[0] = (vp_as_scissor->minx + vp_as_scissor->maxx) / 2.0;
	vp.translate[1] = (vp_as_scissor->miny + vp_as_scissor->maxy) / 2.0;
	vp.scale[0] = vp_as_scissor->maxx - vp.translate[0];
	vp.scale[1] = vp_as_scissor->maxy - vp.translate[1];

	/* Treat a 0x0 viewport as 1x1 to prevent division by zero. */
	if (vp_as_scissor->minx == vp_as_scissor->maxx)
		vp.scale[0] = 0.5;
	if (vp_as_scissor->miny == vp_as_scissor->maxy)
		vp.scale[1] = 0.5;

	/* Find the biggest guard band that is inside the supported viewport
	 * range. The guard band is specified as a horizontal and vertical
	 * distance from (0,0) in clip space.
	 *
	 * This is done by applying the inverse viewport transformation
	 * on the viewport limits to get those limits in clip space.
	 *
	 * Use a limit one pixel smaller to allow for some precision error.
	 */
	max_range = GET_MAX_VIEWPORT_RANGE(rctx) - 1;
	left   = (-max_range - vp.translate[0]) / vp.scale[0];
	right  = ( max_range - vp.translate[0]) / vp.scale[0];
	top    = (-max_range - vp.translate[1]) / vp.scale[1];
	bottom = ( max_range - vp.translate[1]) / vp.scale[1];

	assert(left <= -1 && top <= -1 && right >= 1 && bottom >= 1);

	guardband_x = MIN2(-left, right);
	guardband_y = MIN2(-top, bottom);

	float discard_x = 1.0;
	float discard_y = 1.0;
	float distance = rctx->current_clip_discard_distance;

	/* Add half the point size / line width */
	discard_x += distance / (2.0 * vp.scale[0]);
	discard_y += distance / (2.0 * vp.scale[1]);

	/* Discard primitives that would lie entirely outside the viewport area. */
	discard_x = MIN2(discard_x, guardband_x);
	discard_y = MIN2(discard_y, guardband_y);

	/* If any of the GB registers is updated, all of them must be updated. */
	if (rctx->gfx_level >= CAYMAN)
		radeon_set_context_reg_seq(cs, CM_R_028BE8_PA_CL_GB_VERT_CLIP_ADJ, 4);
	else
		radeon_set_context_reg_seq(cs, R600_R_028C0C_PA_CL_GB_VERT_CLIP_ADJ, 4);

	radeon_emit(cs, fui(guardband_y)); /* R_028BE8_PA_CL_GB_VERT_CLIP_ADJ */
	radeon_emit(cs, fui(discard_y)); /* R_028BEC_PA_CL_GB_VERT_DISC_ADJ */
	radeon_emit(cs, fui(guardband_x)); /* R_028BF0_PA_CL_GB_HORZ_CLIP_ADJ */
	radeon_emit(cs, fui(discard_x)); /* R_028BF4_PA_CL_GB_HORZ_DISC_ADJ */
}

static void r600_emit_scissors(struct r600_common_context *rctx, struct r600_atom *atom)
{
	struct radeon_cmdbuf *cs = &rctx->gfx.cs;
	struct pipe_scissor_state *states = rctx->scissors.states;
	unsigned mask = rctx->scissors.dirty_mask;
	bool scissor_enabled = rctx->scissor_enabled;
	struct r600_signed_scissor max_vp_scissor;
	int i;

	/* The simple case: Only 1 viewport is active. */
	if (!rctx->vs_writes_viewport_index) {
		struct r600_signed_scissor *vp = &rctx->viewports.as_scissor[0];

		if (!(mask & 1))
			return;

		radeon_set_context_reg_seq(cs, R_028250_PA_SC_VPORT_SCISSOR_0_TL, 2);
		r600_emit_one_scissor(rctx, cs, vp, scissor_enabled ? &states[0] : NULL);
		r600_emit_guardband(rctx, vp);
		rctx->scissors.dirty_mask &= ~1; /* clear one bit */
		return;
	}

	/* Shaders can draw to any viewport. Make a union of all viewports. */
	max_vp_scissor = rctx->viewports.as_scissor[0];
	for (i = 1; i < R600_MAX_VIEWPORTS; i++)
		r600_scissor_make_union(&max_vp_scissor,
				      &rctx->viewports.as_scissor[i]);

	while (mask) {
		int start, count, i;

		u_bit_scan_consecutive_range(&mask, &start, &count);

		radeon_set_context_reg_seq(cs, R_028250_PA_SC_VPORT_SCISSOR_0_TL +
					       start * 4 * 2, count * 2);
		for (i = start; i < start+count; i++) {
			r600_emit_one_scissor(rctx, cs, &rctx->viewports.as_scissor[i],
					      scissor_enabled ? &states[i] : NULL);
		}
	}
	r600_emit_guardband(rctx, &max_vp_scissor);
	rctx->scissors.dirty_mask = 0;
}

static void r600_set_viewport_states(struct pipe_context *ctx,
				     unsigned start_slot,
				     unsigned num_viewports,
				     const struct pipe_viewport_state *state)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	unsigned mask;
	int i;

	for (i = 0; i < num_viewports; i++) {
		unsigned index = start_slot + i;

		rctx->viewports.states[index] = state[i];
		r600_get_scissor_from_viewport(rctx, &state[i],
					       &rctx->viewports.as_scissor[index]);
	}

	mask = ((1 << num_viewports) - 1) << start_slot;
	rctx->viewports.dirty_mask |= mask;
	rctx->viewports.depth_range_dirty_mask |= mask;
	rctx->scissors.dirty_mask |= mask;
	rctx->set_atom_dirty(rctx, &rctx->viewports.atom, true);
	rctx->set_atom_dirty(rctx, &rctx->scissors.atom, true);
}

static void r600_emit_one_viewport(struct r600_common_context *rctx,
				   struct pipe_viewport_state *state)
{
	struct radeon_cmdbuf *cs = &rctx->gfx.cs;

	radeon_emit(cs, fui(state->scale[0]));
	radeon_emit(cs, fui(state->translate[0]));
	radeon_emit(cs, fui(state->scale[1]));
	radeon_emit(cs, fui(state->translate[1]));
	radeon_emit(cs, fui(state->scale[2]));
	radeon_emit(cs, fui(state->translate[2]));
}

static void r600_emit_viewports(struct r600_common_context *rctx)
{
	struct radeon_cmdbuf *cs = &rctx->gfx.cs;
	struct pipe_viewport_state *states = rctx->viewports.states;
	unsigned mask = rctx->viewports.dirty_mask;

	/* The simple case: Only 1 viewport is active. */
	if (!rctx->vs_writes_viewport_index) {
		if (!(mask & 1))
			return;

		radeon_set_context_reg_seq(cs, R_02843C_PA_CL_VPORT_XSCALE, 6);
		r600_emit_one_viewport(rctx, &states[0]);
		rctx->viewports.dirty_mask &= ~1; /* clear one bit */
		return;
	}

	while (mask) {
		int start, count, i;

		u_bit_scan_consecutive_range(&mask, &start, &count);

		radeon_set_context_reg_seq(cs, R_02843C_PA_CL_VPORT_XSCALE +
					       start * 4 * 6, count * 6);
		for (i = start; i < start+count; i++)
			r600_emit_one_viewport(rctx, &states[i]);
	}
	rctx->viewports.dirty_mask = 0;
}

static void r600_emit_depth_ranges(struct r600_common_context *rctx)
{
	struct radeon_cmdbuf *cs = &rctx->gfx.cs;
	struct pipe_viewport_state *states = rctx->viewports.states;
	unsigned mask = rctx->viewports.depth_range_dirty_mask;
	float zmin, zmax;

	/* The simple case: Only 1 viewport is active. */
	if (!rctx->vs_writes_viewport_index) {
		if (!(mask & 1))
			return;

		util_viewport_zmin_zmax(&states[0], rctx->clip_halfz, &zmin, &zmax);

		radeon_set_context_reg_seq(cs, R_0282D0_PA_SC_VPORT_ZMIN_0, 2);
		radeon_emit(cs, fui(zmin));
		radeon_emit(cs, fui(zmax));
		rctx->viewports.depth_range_dirty_mask &= ~1; /* clear one bit */
		return;
	}

	while (mask) {
		int start, count, i;

		u_bit_scan_consecutive_range(&mask, &start, &count);

		radeon_set_context_reg_seq(cs, R_0282D0_PA_SC_VPORT_ZMIN_0 +
					   start * 4 * 2, count * 2);
		for (i = start; i < start+count; i++) {
			util_viewport_zmin_zmax(&states[i], rctx->clip_halfz, &zmin, &zmax);
			radeon_emit(cs, fui(zmin));
			radeon_emit(cs, fui(zmax));
		}
	}
	rctx->viewports.depth_range_dirty_mask = 0;
}

static void r600_emit_viewport_states(struct r600_common_context *rctx,
				      struct r600_atom *atom)
{
	r600_emit_viewports(rctx);
	r600_emit_depth_ranges(rctx);
}

/**
 * Normally, we only emit 1 viewport and 1 scissor if no shader is using
 * the VIEWPORT_INDEX output, and emitting the other viewports and scissors
 * is delayed. When a shader with VIEWPORT_INDEX appears, this should be
 * called to emit the rest.
 */
void r600_update_vs_writes_viewport_index(struct r600_common_context *rctx,
					  struct tgsi_shader_info *info)
{
	bool vs_window_space;

	if (!info)
		return;

	/* When the VS disables clipping and viewport transformation. */
	vs_window_space =
		info->properties[TGSI_PROPERTY_VS_WINDOW_SPACE_POSITION];

	if (rctx->vs_disables_clipping_viewport != vs_window_space) {
		rctx->vs_disables_clipping_viewport = vs_window_space;
		rctx->scissors.dirty_mask = (1 << R600_MAX_VIEWPORTS) - 1;
		rctx->set_atom_dirty(rctx, &rctx->scissors.atom, true);
	}

	/* Viewport index handling. */
	rctx->vs_writes_viewport_index = info->writes_viewport_index;
	if (!rctx->vs_writes_viewport_index)
		return;

	if (rctx->scissors.dirty_mask)
	    rctx->set_atom_dirty(rctx, &rctx->scissors.atom, true);

	if (rctx->viewports.dirty_mask ||
	    rctx->viewports.depth_range_dirty_mask)
	    rctx->set_atom_dirty(rctx, &rctx->viewports.atom, true);
}

static void r600_emit_window_rectangles(struct r600_common_context *rctx,
					struct r600_atom *atom)
{
	/* There are four clipping rectangles. Their corner coordinates are inclusive.
	 * Every pixel is assigned a number from 0 and 15 by setting bits 0-3 depending
	 * on whether the pixel is inside cliprects 0-3, respectively. For example,
	 * if a pixel is inside cliprects 0 and 1, but outside 2 and 3, it is assigned
	 * the number 3 (binary 0011).
	 *
	 * If CLIPRECT_RULE & (1 << number), the pixel is rasterized.
	 */
        struct radeon_cmdbuf *cs = &rctx->gfx.cs;
	static const unsigned outside[4] = {
		/* outside rectangle 0 */
		V_02820C_OUT |
		V_02820C_IN_1 |
		V_02820C_IN_2 |
		V_02820C_IN_21 |
		V_02820C_IN_3 |
		V_02820C_IN_31 |
		V_02820C_IN_32 |
		V_02820C_IN_321,
		/* outside rectangles 0, 1 */
		V_02820C_OUT |
		V_02820C_IN_2 |
		V_02820C_IN_3 |
		V_02820C_IN_32,
		/* outside rectangles 0, 1, 2 */
		V_02820C_OUT |
		V_02820C_IN_3,
		/* outside rectangles 0, 1, 2, 3 */
		V_02820C_OUT,
	};
	const unsigned disabled = 0xffff; /* all inside and outside cases */
	unsigned num_rectangles = rctx->window_rectangles.number;
	struct pipe_scissor_state *rects = rctx->window_rectangles.states;
	unsigned rule;

	assert(num_rectangles <= R600_MAX_WINDOW_RECTANGLES);

	if (num_rectangles == 0)
		rule = disabled;
	else if (rctx->window_rectangles.include)
		rule = ~outside[num_rectangles - 1];
	else
		rule = outside[num_rectangles - 1];

	radeon_set_context_reg_seq(cs, R_02820C_PA_SC_CLIPRECT_RULE, 1);
	radeon_emit(cs, rule);

	if (num_rectangles == 0)
		return;

	radeon_set_context_reg_seq(cs, R_028210_PA_SC_CLIPRECT_0_TL,
				   num_rectangles * 2);
	for (unsigned i = 0; i < num_rectangles; i++) {
		radeon_emit(cs, S_028210_TL_X(rects[i].minx) |
			    S_028210_TL_Y(rects[i].miny));
		radeon_emit(cs, S_028214_BR_X(rects[i].maxx) |
			    S_028214_BR_Y(rects[i].maxy));
	}
}

static void r600_set_window_rectangles(struct pipe_context *ctx,
				       bool include,
				       unsigned num_rectangles,
				       const struct pipe_scissor_state *rects)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;

	rctx->window_rectangles.number = num_rectangles;
	rctx->window_rectangles.include = include;

	if (num_rectangles)
		memcpy(rctx->window_rectangles.states, rects,
		       sizeof(*rects) * num_rectangles);

	rctx->set_atom_dirty(rctx, &rctx->window_rectangles.atom, true);
}

void r600_init_viewport_functions(struct r600_common_context *rctx)
{
	rctx->scissors.atom.emit = r600_emit_scissors;
	rctx->viewports.atom.emit = r600_emit_viewport_states;
	rctx->window_rectangles.atom.emit = r600_emit_window_rectangles;

	rctx->scissors.atom.num_dw = (2 + 16 * 2) + 6;
	rctx->viewports.atom.num_dw = 2 + 16 * 6;

	rctx->b.set_scissor_states = r600_set_scissor_states;
	rctx->b.set_viewport_states = r600_set_viewport_states;
	rctx->b.set_window_rectangles = r600_set_window_rectangles;
}
