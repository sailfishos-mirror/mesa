# Copyright © 2026 Daniel Lang
# SPDX-License-Identifier: MIT

layout_files = [
  'eil_format.c',
  'eil_format.h',
]

libetnaviv_layout = static_library(
  'libetnaviv_layout',
  layout_files,
  dependencies: idep_mesautil,
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

if with_tests
  test(
    'etnaviv_layout_tests',
    executable(
      'etnaviv_layout_tests',
      files(
        'tests/format-test.cpp',
      ),
      c_args : [c_msvc_compat_args, no_override_init_args],
      gnu_symbol_visibility : 'hidden',
      dependencies: [idep_gtest, idep_mesautil],
      link_with : [libetnaviv_layout],
    ),
    suite : ['etnaviv'],
    protocol : 'gtest',
  )
endif
