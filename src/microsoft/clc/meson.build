# Copyright © Microsoft Corporation
# SPDX-License-Identifier: MIT

files_libclc_compiler = files(
  'clc_compiler.c',
  'clc_nir.c',
)

libclc_compiler = shared_library(
  'clon12compiler',
  [files_libclc_compiler, sha1_h],
  vs_module_defs : 'clon12compiler.def',
  include_directories : [inc_include, inc_src, inc_spirv],
  dependencies: [idep_mesaclc, idep_nir_headers, dep_version, idep_mesautil,
                 idep_libdxil_compiler, idep_nir, idep_vtn],
  install : true,
  name_prefix : ''  # otherwise mingw will create libclon12compiler.dll
)

if dep_dxheaders.found() and host_machine.system() == 'windows' and with_tests
  clc_compiler_test = executable('clc_compiler_test',
    ['clc_compiler_test.cpp', 'compute_test.cpp'],
    link_with : [libclc_compiler],
    dependencies : [idep_gtest, idep_mesautil, idep_libdxil_compiler, dep_dxheaders,
                    dep_spirv_tools, idep_compiler],
    include_directories : [inc_include, inc_src, inc_spirv],
  )

  if build_machine.system() == 'windows'
    test('clc_compiler_test', clc_compiler_test, timeout: 180, protocol : 'gtest')
  endif

endif
