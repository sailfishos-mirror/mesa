libtorx_files = files(
   'torx_device.c',
   'torx_target.c',
)

libtorx = shared_library(
  'torx',
  [libtorx_files],
  include_directories : [ inc_src, inc_util, inc_include, inc_gallium, inc_gallium_aux, inc_gallium_winsys, inc_gallium_drivers ],
  link_whole : [ libtorx_st ],
  link_with : [ libpipe_loader_static, libws_null, libwsw, libswdri, libswkmsdri, libgallium ],
  gnu_symbol_visibility : 'hidden',
  link_args : ld_args_build_id,
  override_options : ['b_lundef=false'],
  dependencies : [
    dep_executorch,
    driver_etnaviv,
    driver_rocket,
    driver_ethosu,
    idep_nir,
    idep_mesautil,
  ],
  install : true,
)