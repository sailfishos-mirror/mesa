# Copyright © Microsoft Corporation
# SPDX-License-Identifier: MIT

files_libmesaclc = files(
  'clc.c',
  'clc_helpers.cpp',
  'nir_load_libclc.c',
)

_libmesaclc_c_args = []
_libmesaclc_cpp_args = ['-DLLVM_LIB_DIR="@0@"'.format(llvm_libdir)]
_libmesaclc_sources = []

if not _shared_llvm or \
    get_option('mesa-clc-bundle-headers') == 'enabled'
  # LLVM 16 moved clang header path from using full version to only major version
  if dep_llvm.version().version_compare('< 16')
    # Prior to LLVM 16, this path used a full version
    clang_version_dir = dep_llvm.version()
  else
    # LLVM 16 changed to only using a major version
    clang_version_dir = dep_llvm.version().split('.')[0]
  endif
  clang_resource_dir = join_paths(llvm_libdir, 'clang', clang_version_dir, 'include')

  opencl_c_base_h = custom_target(
    'opencl-c-base.h',
    input : [files_xxd, join_paths(clang_resource_dir, 'opencl-c-base.h')],
    output : 'opencl-c-base.h.h',
    command : [prog_python, '@INPUT@', '@OUTPUT@', '-n', 'opencl_c_base_source'],
  )

  opencl_c_h = custom_target(
    'opencl-c.h',
    input : [files_xxd, join_paths(clang_resource_dir, 'opencl-c.h')],
    output : 'opencl-c.h.h',
    command : [prog_python, '@INPUT@', '@OUTPUT@', '-n', 'opencl_c_source'],
  )

  _libmesaclc_sources += [opencl_c_base_h, opencl_c_h]
  _libmesaclc_cpp_args += ['-DUSE_STATIC_OPENCL_C_H=1']
endif

_basedir = dep_clc.get_variable(pkgconfig : 'libexecdir')

_static_libclc = get_option('static-libclc')
if _static_libclc.length() > 0
  if _static_libclc.contains('all')
    _static_libclc = ['spirv', 'spirv64']
  endif
  prog_zstd = find_program('zstd', required : false, native : true)
  _zstd_static_libclc = dep_zstd.found() and prog_zstd.found()
  if _zstd_static_libclc
    _libmesaclc_c_args += '-DHAVE_STATIC_LIBCLC_ZSTD'
  endif
  foreach s : _static_libclc
    _libmesaclc_c_args += '-DHAVE_STATIC_LIBCLC_@0@'.format(s.to_upper())
    f = '@0@-mesa3d-.spv'.format(s)
    _libclc_file = _basedir / f
    if _zstd_static_libclc
      _libclc_file = custom_target(
        '@0@.zstd'.format(f),
        command : [prog_zstd, '-f', '@INPUT@', '-o', '@OUTPUT@'],
        input : [_libclc_file],
        output : '@0@.zstd'.format(f),
      )
    endif
    files_libmesaclc += custom_target(
      '@0@.h'.format(f),
      command : [
        prog_python, files_xxd, '-b', '@INPUT@', '@OUTPUT@',
        '-n', 'libclc_@0@_mesa3d_spv'.format(s),
      ],
      input : [_libclc_file],
      output : '@0@.h'.format(f),
      depend_files : files_xxd,
    )
  endforeach
else
  _libmesaclc_c_args += ['-DDYNAMIC_LIBCLC_PATH="@0@/"'.format(_basedir)]
  if not cc.has_function('mmap')
    error('mmap required for dynamic libCLC loading')
  endif
endif

has_spirv_link_workaround = cpp.has_member(
  'spvtools::LinkerOptions',
  'SetAllowPtrTypeMismatch(true)',
  prefix : [
    '#include <spirv-tools/linker.hpp>',
  ],
  dependencies : dep_spirv_tools,
)
if dep_llvm.version().version_compare('>= 17.0') and not has_spirv_link_workaround
  error('SPIRV-Tools doesn\'t contain https://github.com/KhronosGroup/SPIRV-Tools/pull/5534\n')
endif

if has_spirv_link_workaround
  _libmesaclc_c_args += ['-DHAS_SPIRV_LINK_LLVM_WORKAROUND=1']
endif

_libmesaclc = static_library(
  'libmesaclc',
  files_libmesaclc,
  sources: _libmesaclc_sources,
  include_directories : [inc_include, inc_src, inc_spirv],
  c_args : _libmesaclc_c_args,
  cpp_args : [_libmesaclc_cpp_args, _libmesaclc_c_args],
  dependencies: [idep_nir, dep_clang, dep_llvm, dep_llvmspirvlib,
                 idep_mesautil, dep_spirv_tools, idep_vtn, dep_version]
)

_idep_mesaclc_link_args = []
if _shared_llvm
  _idep_mesaclc_link_args += cc.get_supported_link_arguments('-fPIC')
endif

idep_mesaclc = declare_dependency(
  link_with : _libmesaclc,
  include_directories : include_directories('.'),
  link_args : _idep_mesaclc_link_args,
)

if get_option('mesa-clc') != 'system' and (with_driver_using_cl or \
                                           get_option('install-mesa-clc'))
  prog_mesa_clc = executable(
    'mesa_clc',
    ['mesa_clc.c'],
    include_directories : [inc_include, inc_src],
    c_args : [pre_args, no_override_init_args],
    link_args : [ld_args_build_id],
    dependencies : [idep_mesaclc, dep_llvm, dep_spirv_tools, idep_getopt],
    # If we can run host binaries directly, just build mesa_clc for the host.
    # Most commonly this happens when doing a cross compile from an x86_64 build
    # machine to an x86 host
    native : not meson.can_run_host_binaries(),
    install : get_option('install-mesa-clc'),
  )
endif
