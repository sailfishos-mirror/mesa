/*
 * Copyright © 2023 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "helpers.h"
#include "test_d3d11_derivs-spirv.h"

using namespace aco;

BEGIN_TEST(d3d11_derivs.simple)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec2 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler2D tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, in_coord);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   //>> v1: %x = v_interp_p2_f32 %_, %_:m0, (kill)%_ attr0.x
   //>> v1: %y = v_interp_p2_f32 (kill)%_, (kill)%_:m0, (kill)%_ attr0.y
   //>> lv2: %wqm = p_start_linear_vgpr (kill)%x, (kill)%y
   //>> BB1
   //>> v4: %_ = image_sample (kill)%_, (kill)%_, v1: undef, %wqm 2d
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_interp_p2_f32_e32 v#rx_tmp, v#_, attr0.x                                         ; $_
   //>> v_interp_p2_f32_e32 v#ry_tmp, v#_, attr0.y                                         ; $_
   //>> v_mov_b32_e32 v#rx, v#rx_tmp                                                       ; $_
   //>> v_mov_b32_e32 v#ry, v#ry_tmp                                                       ; $_
   //>> image_sample v[#_:#_], v[#rx:#ry], s[#_:#_], s[#_:#_] dmask:0xf dim:SQ_RSRC_IMG_2D ; $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs.constant)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in float in_coord;
      layout(location = 0) out float out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in float in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler2D tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, vec2(in_coord, -0.5));
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   //>> v1: %x = v_interp_p2_f32 (kill)%_, (kill)%_:m0, (kill)%_ attr0.x
   //>> lv2: %wqm = p_start_linear_vgpr (kill)%x, -0.5
   //>> BB1
   //>> v4: %_ = image_sample (kill)%_, (kill)%_, v1: undef, %wqm 2d
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_mov_b32_e32 v#ry, -0.5                                                           ; $_
   //>> v_interp_p2_f32_e32 v#rx_tmp, v#_, attr0.x                                         ; $_
   //>> v_mov_b32_e32 v#rx, v#rx_tmp                                                       ; $_
   //>> image_sample v[#_:#_], v[#rx:#ry], s[#_:#_], s[#_:#_] dmask:0xf dim:SQ_RSRC_IMG_2D ; $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs.discard)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec2 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler2D tex;
      void main() {
         if (gl_FragCoord.y > 1.0)
            discard;
         out_color = texture(tex, in_coord);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   /* The discard gets emitted as demote_if. */
   //>> s2: %_:exec,  s1: (kill)%_:scc = s_wqm_b64 %_
   //! p_exit_early_if_not %_:exec
   //>> v4: %_ = image_sample (kill)%_, (kill)%_, v1: undef, (kill)%_, (kill)%_ 2d
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");
END_TEST

BEGIN_TEST(d3d11_derivs.bias)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec2 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler2D tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, in_coord, gl_FragCoord.x);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   //>> s2: %_:s[0-1], s1: %_:s[2], s1: %_:s[3], s1: %_:s[4], v2: %_:v[0-1], v1: %bias:v[2] = p_startpgm
   //>> lv3: %wqm = p_start_linear_vgpr v1: undef, (kill)%_, (kill)%_
   //>> BB1
   //>> v4: %_ = image_sample_b (kill)%_, (kill)%_, v1: undef, %wqm, (kill)%bias 2d
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_interp_p2_f32_e32 v#rx_tmp, v#_, attr0.x                                                   ; $_
   //>> v_interp_p2_f32_e32 v#ry_tmp, v#_, attr0.y                                                   ; $_
   //>> v_mov_b32_e32 v#rx, v#rx_tmp                                                                 ; $_
   //>> v_mov_b32_e32 v#ry, v#ry_tmp                                                                 ; $_
   //>> BB1:
   //>> image_sample_b v[#_:#_], [v#rb, v#rx, v#ry], s[#_:#_], s[#_:#_] dmask:0xf dim:SQ_RSRC_IMG_2D ; $_ $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs.offset)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec2 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler2D tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = textureOffset(tex, in_coord, ivec2(1, 2));
      }
   );
   // clang-format on

   /* Use GFX9 because we should have at least one test which doesn't use NSA. */
   PipelineBuilder pbld(get_vk_device(GFX9));
   pbld.add_vsfs(vs, fs);

   //>> lv3: %wqm = p_start_linear_vgpr v1: undef, (kill)%_, (kill)%_
   //>> BB1
   //>> v1: %offset = p_parallelcopy 0x201
   //>> v4: %_ = image_sample_o (kill)%_, (kill)%_, v1: undef, %wqm, (kill)%offset 2d
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_interp_p2_f32_e32 v#rx_tmp, v#_, attr0.x                        ; $_
   //>> v_interp_p2_f32_e32 v#ry_tmp, v#_, attr0.y                        ; $_
   //>> v_mov_b32_e32 v#rx, v#rx_tmp                                      ; $_
   //>> v_mov_b32_e32 v#ry, v#ry_tmp                                      ; $_
   //>> BB1:
   //>> v_mov_b32_e32 v#ro_tmp, 0x201                                     ; $_ $_
   //>> v_mov_b32_e32 v#ro, v#r0_tmp                                      ; $_
   //; success = ro+1 == rx and ro+2 == ry
   //>> image_sample_o v[#_:#_], v[#ro:#rx], s[#_:#_], s[#_:#_] dmask:0xf ; $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs.array)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec3 in_coord;
      layout(location = 0) out vec3 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec3 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler2DArray tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, in_coord);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   //>> v1: %layer = v_rndne_f32 (kill)%_
   //>> lv3: %wqm = p_start_linear_vgpr (kill)%_, (kill)%_, (kill)%layer
   //>> BB1
   //>> v4: %_ = image_sample (kill)%_, (kill)%_, v1: undef, %wqm 2darray da
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_interp_p2_f32_e32 v#rl_tmp, v#_, attr0.z                                               ; $_
   //>> v_interp_p2_f32_e32 v#rx_tmp, v#_, attr0.x                                               ; $_
   //>> v_interp_p2_f32_e32 v#ry_tmp, v#_, attr0.y                                               ; $_
   //>> v_rndne_f32_e32 v#rl_tmp, v#rl_tmp                                                       ; $_
   //>> v_mov_b32_e32 v#rx, v#rx_tmp                                                             ; $_
   //>> v_mov_b32_e32 v#ry, v#ry_tmp                                                             ; $_
   //>> v_mov_b32_e32 v#rl, v#rl_tmp                                                             ; $_
   //>> BB1:
   //; success = rx+1 == ry and rx+2 == rl
   //>> image_sample v[#_:#_], v[#rx:#rl], s[#_:#_], s[#_:#_] dmask:0xf dim:SQ_RSRC_IMG_2D_ARRAY ; $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs.bias_array)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec3 in_coord;
      layout(location = 0) out vec3 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec3 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler2DArray tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, in_coord, gl_FragCoord.x);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   //>> s2: %_:s[0-1], s1: %_:s[2], s1: %_:s[3], s1: %_:s[4], v2: %_:v[0-1], v1: %bias:v[2] = p_startpgm
   //>> v1: %layer = v_rndne_f32 (kill)%_
   //>> lv4: %wqm = p_start_linear_vgpr v1: undef, (kill)%_, (kill)%_, (kill)%layer
   //>> BB1
   //>> v4: %_ = image_sample_b (kill)%_, (kill)%_, v1: undef, %wqm, (kill)%bias 2darray da
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_interp_p2_f32_e32 v#rl_tmp, v#_, attr0.z                                                             ; $_
   //>> v_interp_p2_f32_e32 v#rx_tmp, v#_, attr0.x                                                             ; $_
   //>> v_interp_p2_f32_e32 v#ry_tmp, v#_, attr0.y                                                             ; $_
   //>> v_rndne_f32_e32 v#rl_tmp, v#rl_tmp                                                                     ; $_
   //>> v_mov_b32_e32 v#rx, v#rx_tmp                                                                           ; $_
   //>> v_mov_b32_e32 v#ry, v#ry_tmp                                                                           ; $_
   //>> v_mov_b32_e32 v#rl, v#rl_tmp                                                                           ; $_
   //>> BB1:
   //>> image_sample_b v[#_:#_], [v2, v#rx, v#ry, v#rl], s[#_:#_], s[#_:#_] dmask:0xf dim:SQ_RSRC_IMG_2D_ARRAY ; $_ $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs._1d_gfx9)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in float in_coord;
      layout(location = 0) out float out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in float in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler1D tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, in_coord);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX9));
   pbld.add_vsfs(vs, fs);

   //>> v1: %x = v_interp_p2_f32 (kill)%_, (kill)%_:m0, (kill)%_ attr0.x
   //>> lv2: %wqm = p_start_linear_vgpr (kill)%x, 0.5
   //>> BB1
   //>> v4: %_ = image_sample (kill)%_, (kill)%_, v1: undef, %wqm 2d
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_mov_b32_e32 v#ry, 0.5                                   ; $_
   //>> v_interp_p2_f32_e32 v#rx_tmp, v#_, attr0.x                ; $_
   //>> v_mov_b32_e32 v#rx, v#rx_tmp                              ; $_
   //; success = rx+1 == ry
   //>> image_sample v[#_:#_], v#rx, s[#_:#_], s[#_:#_] dmask:0xf ; $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs._1d_array_gfx9)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec2 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler1DArray tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, in_coord);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX9));
   pbld.add_vsfs(vs, fs);

   //>> v1: %layer = v_rndne_f32 (kill)%_
   //>> v1: %x = v_interp_p2_f32 (kill)%_, (kill)%_:m0, (kill)%_ attr0.x
   //>> lv3: %wqm = p_start_linear_vgpr (kill)%x, 0.5, (kill)%layer
   //>> BB1
   //>> v4: %_ = image_sample (kill)%_, (kill)%_, v1: undef, %wqm 2darray da
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_mov_b32_e32 v#ry, 0.5                                      ; $_
   //>> v_interp_p2_f32_e32 v#rl_tmp, v#_, attr0.y                   ; $_
   //>> v_interp_p2_f32_e32 v#rx_tmp, v#_, attr0.x                   ; $_
   //>> v_rndne_f32_e32 v#rl_tmp, v#rl_tmp                           ; $_
   //>> v_mov_b32_e32 v#rx, v#rx_tmp                                 ; $_
   //>> v_mov_b32_e32 v#rl, v#rl_tmp                                 ; $_
   //>> BB1:
   //; success = rx+1 == ry and rx+2 == rl
   //>> image_sample v[#_:#_], v#rx, s[#_:#_], s[#_:#_] dmask:0xf da ; $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs.cube)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec3 in_coord;
      layout(location = 0) out vec3 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec3 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform samplerCube tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, in_coord);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   //>> v1: %face = v_cubeid_f32 (kill)%_, (kill)%_, (kill)%_
   //>> v1: %x = v_fmaak_f32 (kill)%_, %_, 0x3fc00000
   //>> v1: %y = v_fmaak_f32 (kill)%_, (kill)%_, 0x3fc00000
   //>> lv3: %wqm = p_start_linear_vgpr (kill)%x, (kill)%y, (kill)%face
   //>> BB1
   //>> v4: %_ = image_sample (kill)%_, (kill)%_, v1: undef, %wqm cube da
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_cubeid_f32 v#rf_tmp, v#_, v#_, v#_                                                 ; $_ $_
   //>> v_mov_b32_e32 v#rf, v#rf_tmp                                                         ; $_
   //>> v_fmaak_f32 v#rx_tmp, v#_, v#_, 0x3fc00000                                           ; $_ $_
   //>> v_fmaak_f32 v#ry_tmp, v#_, v#_, 0x3fc00000                                           ; $_ $_
   //>> v_lshrrev_b64 v[#rx:#ry], 0, v[#rx_tmp:#ry_tmp]                                      ; $_ $_
   //; success = rx+1 == ry and rx+2 == rf
   //>> image_sample v[#_:#_], v[#rx:#rf], s[#_:#_], s[#_:#_] dmask:0xf dim:SQ_RSRC_IMG_CUBE ; $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs.cube_array)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec4 in_coord;
      layout(location = 0) out vec4 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec4 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform samplerCubeArray tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, in_coord);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   //>> v1: %face = v_cubeid_f32 (kill)%_, (kill)%_, (kill)%_
   //>> v1: %x = v_fmaak_f32 (kill)%_, %_, 0x3fc00000
   //>> v1: %y = v_fmaak_f32 (kill)%_, (kill)%_, 0x3fc00000
   //>> v1: %layer = v_rndne_f32 (kill)%_
   //>> v1: %face_layer = v_fmamk_f32 (kill)%layer, (kill)%face, 0x41000000
   //>> lv3: %wqm = p_start_linear_vgpr (kill)%x, (kill)%y, (kill)%face_layer
   //>> BB1
   //>> v4: %_ = image_sample (kill)%_, (kill)%_, v1: undef, %wqm cube da
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");

   //>> v_cubeid_f32 v#rf, v#_, v#_, v#_                                                      ; $_ $_

   //>> v_fmamk_f32 v#rlf_tmp, v#rl, 0x41000000, v#rf                                         ; $_ $_
   //>> v_mov_b32_e32 v#rlf, v#rlf_tmp                                                        ; $_
   //>> v_fmaak_f32 v#rx_tmp, v#_, v#_, 0x3fc00000                                            ; $_ $_
   //>> v_fmaak_f32 v#ry_tmp, v#_, v#_, 0x3fc00000                                            ; $_ $_
   //>> v_lshrrev_b64 v[#rx:#ry], 0, v[#rx_tmp:#ry_tmp]                                       ; $_ $_

   //>> BB1:
   //; success = rx+1 == ry and rx+2 == rlf
   //>> image_sample v[#_:#_], v[#rx:#rlf], s[#_:#_], s[#_:#_] dmask:0xf dim:SQ_RSRC_IMG_CUBE ; $_ $_
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "Assembly");
END_TEST

BEGIN_TEST(d3d11_derivs.dfdxy)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec2 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler2D tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = vec4(dFdxFine(in_coord.x), dFdyCoarse(in_coord.y), textureLod(tex, vec2(0.5), 0.0).xy);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   /* Must be before BB1 */
   //>> v1: %_ = v_subrev_f32 (kill)%_, (kill)%_ quad_perm:[0,0,2,2] bound_ctrl:1 fi
   //>> v1: %_ = v_subrev_f32 (kill)%_, (kill)%_ quad_perm:[0,0,0,0] bound_ctrl:1 fi
   //>> BB1
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");
END_TEST

/* Ensure the BC optimize transform is done after ac_nir_lower_tex. */
BEGIN_TEST(d3d11_derivs.bc_optimize)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec2 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec4 out_color;
      layout(binding = 0) uniform sampler2D tex;
      void main() {
         out_color = vec4(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = texture(tex, vec2(in_coord.x, interpolateAtCentroid(in_coord.y)));
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   //>> v1: %y_coord2 = v_cndmask_b32 (kill)%_, %_, (kill)%_
   //>> v1: %x = v_interp_p2_f32 (kill)%_, %_:m0, (kill)%_ attr0.x
   //>> v1: %y = v_interp_p2_f32 (kill)%y_coord2, (kill)%_:m0, (kill)%_ attr0.y
   //>> lv2: %wqm = p_start_linear_vgpr (kill)%x, (kill)%y
   //>> BB1
   //>> v4: %_ = image_sample (kill)%_, (kill)%_, v1: undef, %wqm 2d
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");
END_TEST

BEGIN_TEST(d3d11_derivs.get_lod)
   // clang-format off
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec2 out_coord;
      void main() {
         out_coord = in_coord;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec2 in_coord;
      layout(location = 0) out vec2 out_color;
      layout(binding = 0) uniform sampler2D tex;
      void main() {
         out_color = vec2(0.0);
         if (gl_FragCoord.x > 1.0)
            out_color = textureQueryLod(tex, in_coord);
      }
   );
   // clang-format on

   PipelineBuilder pbld(get_vk_device(GFX10_3));
   pbld.add_vsfs(vs, fs);

   //>> v1: %x = v_interp_p2_f32 %_, %_:m0, (kill)%_ attr0.x
   //>> v1: %y = v_interp_p2_f32 (kill)%_, (kill)%_:m0, (kill)%_ attr0.y
   //>> lv2: %wqm = p_start_linear_vgpr %x, %y
   //>> v1: %x12_m_x0 = v_subrev_f32 (kill)%x, (kill)%x quad_perm:[0,0,0,0] bound_ctrl:1 fi
   //>> v1: %x1_m_x0 = v_mov_b32 %x12_m_x0 quad_perm:[1,1,1,1] bound_ctrl:1 fi
   //>> v1: %x2_m_x0 = v_mov_b32 (kill)%x12_m_x0 quad_perm:[2,2,2,2] bound_ctrl:1 fi
   //>> v1: %y12_m_y0 = v_subrev_f32 (kill)%y, (kill)%y quad_perm:[0,0,0,0] bound_ctrl:1 fi
   //>> v1: %y1_m_y0 = v_mov_b32 %y12_m_x0 quad_perm:[1,1,1,1] bound_ctrl:1 fi
   //>> v1: %y2_m_y0 = v_mov_b32 (kill)%y12_m_x0 quad_perm:[2,2,2,2] bound_ctrl:1 fi
   //>> BB1
   //>> v2: %_ = image_get_lod (kill)%_, (kill)%_, v1: undef, %wqm 2d
   //>> BB2
   //>> BB6
   //>> p_end_linear_vgpr (kill)%wqm
   pbld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");
END_TEST

BEGIN_TEST(d3d11_derivs.nsa_max)
   for (amd_gfx_level lvl : {GFX10, GFX10_3, GFX11}) {
      if (!setup_cs(NULL, lvl))
         continue;

      PhysReg reg_v0{256};
      PhysReg reg_v6{256 + 6};
      PhysReg reg_v7{256 + 7};
      PhysReg reg_v8{256 + 8};
      PhysReg reg_s0{0};
      PhysReg reg_s8{8};

      //>> p_unit_test 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::zero());

      //~gfx10! v2: %_:v[0-1] = v_lshrrev_b64 0, %_:v[6-7]
      //~gfx10! v1: %_:v[2] = v_mov_b32 %_:v[8]
      //~gfx10! v4: %_:v[0-3] = image_sample_c_b_o  %0:s[0-7], %0:s[8-11],  v1: undef, %_:v[0-5] 2darray da

      //~gfx10_3! v4: %_:v[0-3] = image_sample_c_b_o  %0:s[0-7], %0:s[8-11],  v1: undef, %_:v[6], %_:v[7], %_:v[8], %_:v[3], %_:v[4], %_:v[5] 2darray da

      //~gfx11! v4: %_:v[0-3] = image_sample_c_b_o  %0:s[0-7], %0:s[8-11],  v1: undef, %_:v[6], %_:v[7], %_:v[8], %_:v[3], %_:v[4-5] 2darray da

      Instruction* instr =
         bld.mimg(aco_opcode::image_sample_c_b_o, Definition(reg_v0, v4), Operand(reg_s0, s8),
                  Operand(reg_s8, s4), Operand(v1), Operand(reg_v0, v6.as_linear()),
                  Operand(reg_v6, v1), Operand(reg_v7, v1), Operand(reg_v8, v1));
      instr->mimg().dim = ac_image_2darray;
      instr->mimg().da = true;
      instr->mimg().strict_wqm = true;

      finish_to_hw_instr_test();
   }
END_TEST
