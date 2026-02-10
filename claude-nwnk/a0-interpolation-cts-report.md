# VK CTS validation: fury's a0 interpolation fix

convoy: hq-cv-wr4iu (llvmpipe-rasterizer-validation)

## summary

fury's fix changes interpolation origin from framebuffer (0,0) to vertex 0
position for numerical stability with triangles far from origin.

**result: no regressions, no fixes detected. fix is safe from a CTS perspective.**

## test results

tested dEQP-VK categories focused on rasterization/interpolation.

### dEQP-VK.rasterization.*

| build | passed | failed | not supported |
|-------|--------|--------|---------------|
| baseline | 7448 | 0 | 7631 |
| with fix | 7448 | 0 | 7631 |

### dEQP-VK.draw.*

| build | passed | failed | not supported |
|-------|--------|--------|---------------|
| baseline | 24015 | 0 | 6729 |
| with fix | 24015 | 0 | 6729 |

### dEQP-VK.pipeline.monolithic.multisample.*.verify_interpolation.*

| build | passed | failed | not supported |
|-------|--------|--------|---------------|
| baseline | 15 | 0 | 18 |
| with fix | 15 | 0 | 18 |

## notes

- the fix is designed for numerical stability with triangles far from origin,
  which the CTS may not explicitly stress.
- identical results baseline vs fix suggests no functional regression.
- the "not supported" counts are expected - lavapipe doesn't expose all
  features (shader_tile_image, android external formats, high sample counts, etc).

## files changed

12 files in src/gallium/drivers/llvmpipe/:
- lp_bld_interp.c
- lp_linear.c
- lp_linear_fastpath.c
- lp_linear_interp.c
- lp_linear_priv.h
- lp_linear_sampler.c
- lp_rast.c
- lp_setup_line.c
- lp_setup_point.c
- lp_state_fs_linear.c
- lp_state_setup.c
