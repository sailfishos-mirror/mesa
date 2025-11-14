# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libintel_compiler_gen_files = files(
  'gen.h',
  'gen_enums.h',
  'gen_types.h',
  'gen_helpers.h',

  'gen_private.h',

  'gen_encoding.cpp',
  'gen_encoding_pre_xe.cpp',
)

libintel_compiler_gen = static_library(
  'intel_compiler_gen',
  libintel_compiler_gen_files,
  include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],
  c_args : [no_override_init_args],
  cpp_args : ['-Werror=vla'],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil, idep_intel_dev],
  build_by_default : false,
)

idep_intel_compiler_gen = declare_dependency(
  link_with : [libintel_compiler_gen],
  dependencies : [
    idep_mesautil,
  ],
)

if with_tests
  test(
    'intel_compiler_gen_tests',
    executable(
      'intel_compiler_gen_tests',
      files(
        'gen_encoding_test.cpp',
      ),
      include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],
      cpp_args : ['-Werror=vla'],
      dependencies : [idep_gtest, idep_mesautil, idep_intel_dev,
                      idep_intel_compiler_gen],
    ),
    suite : ['intel'],
    protocol : 'gtest',
  )
endif


