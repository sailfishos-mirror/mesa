# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

# TODO: android?

files_libintel_dev = files(
  'i915/intel_device_info.c',
  'i915/intel_device_info.h',
  'xe/intel_device_info.c',
  'xe/intel_device_info.h',
  'intel_debug.c',
  'intel_debug.h',
  'intel_device_info.c',
  'intel_device_info.h',
  'intel_hwconfig.c',
  'intel_hwconfig.h',
  'intel_kmd.c',
  'intel_kmd.h',
)

intel_dev_wa_src = custom_target('intel_wa.[ch]',
                                 input : ['gen_wa_helpers.py', 'mesa_defs.json'],
                                 output : ['intel_wa.h', 'intel_wa.c'],
                                 command : [prog_python, '@INPUT@', '@OUTPUT@'])

intel_dev_info_gen_src = custom_target('intel_device_info_gen.h',
                                       input : 'intel_device_info_gen_h.py',
                                       depend_files: 'intel_device_info.py',
                                       output : ['intel_device_info_gen.h'],
                                       command : [prog_python, '@INPUT@', '@OUTPUT@'])

intel_dev_serialize_src = custom_target('intel_device_info_serialize_gen.c',
                                        input : 'intel_device_info_serialize_gen_c.py',
                                        depend_files: 'intel_device_info.py',
                                        output : ['intel_device_info_serialize_gen.c'],
                                        command : [prog_python, '@INPUT@', '@OUTPUT@'])

# ensures intel_wa.h exists before implementation files are compiled
idep_intel_dev_wa = declare_dependency(sources : [intel_dev_wa_src[0]])

idep_intel_dev_info_gen = declare_dependency(sources : [intel_dev_info_gen_src[0]])

libintel_dev = static_library(
  'intel_dev',
  [files_libintel_dev, sha1_h, [intel_dev_wa_src]],
  include_directories : [inc_include, inc_src, inc_intel],
  dependencies : [dep_libdrm, idep_mesautil, idep_intel_dev_wa,
                  idep_intel_dev_info_gen],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
)

idep_intel_dev = declare_dependency(
  link_with : libintel_dev,
  dependencies : [idep_intel_dev_wa, idep_intel_dev_info_gen, idep_mesautil],
  sources : [intel_dev_info_gen_src, intel_dev_wa_src[0]],
)

if with_tests
  test('intel_device_info_test',
       executable(
         'intel_device_info_test',
         'intel_device_info_test.c',
         include_directories : [inc_include, inc_src, inc_intel],
         dependencies : idep_intel_dev,
       ),
       suite : ['intel'],
      )
endif

if with_tests and with_tools.contains('drm-shim') and with_tools.contains('intel')
  platforms = [ [ 70,  ['ivb', 'byt'] ],
                [ 75,  ['hsw'] ],
                [ 80,  ['bdw', 'chv'] ],
                [ 90,  ['skl', 'kbl', 'aml', 'cml', 'whl', 'bxt', 'glk'] ],
                [ 110, ['icl', 'ehl', 'jsl'] ],
                [ 120, ['tgl', 'rkl', 'adl', 'rpl', 'dg1', 'sg1'] ],
                [ 125, ['dg2'] ],
              ]
  intel_devinfo_override_test = executable(
    'intel_device_info_override_test',
    'intel_device_info_override_test.c',
    dependencies : [dep_libdrm, idep_intel_dev],
    include_directories : [inc_include, inc_src, inc_intel],
  )
  intel_drm_ld_preload = join_paths(meson.current_build_dir(), '..', 'tools', 'libintel_noop_drm_shim.so')
  foreach _gens : platforms
    gen = _gens[0]
    foreach p : _gens[1]
              test('intel_device_info_override_test_@0@'.format(p),
                   intel_devinfo_override_test,
                   args : '@0@'.format(gen),
                   env : [
                      'LD_PRELOAD=@0@'.format(intel_drm_ld_preload),
                      'INTEL_STUB_GPU_PLATFORM=@0@'.format(p),
                      'STRACEDIR=meson-logs/strace/intel_device_info_override_test_@0@'.format(p),
                   ],
                   suite : ['intel'],
                  )
    endforeach
  endforeach
endif
