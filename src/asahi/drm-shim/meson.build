# Copyright 2019 Broadcom
# SPDX-License-Identifier: MIT

libasahi_noop_drm_shim = shared_library(
  'asahi_noop_drm_shim',
  'asahi_noop.c',
  include_directories: [inc_include, inc_src],
  dependencies: [dep_drm_shim, dep_valgrind],
  gnu_symbol_visibility : 'hidden',
  install : true,
)
