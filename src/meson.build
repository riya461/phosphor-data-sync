# SPDX-License-Identifier: Apache-2.0

phosphor_dbus_interfaces = dependency('phosphor-dbus-interfaces')
phosphor_logging = dependency('phosphor-logging')
sdbusplus = dependency('sdbusplus')

rbmc_data_sync_sources = [
    'rbmc_data_sync_main.cpp'
  ]

rbmc_data_sync_dependencies = [
    phosphor_dbus_interfaces,
    phosphor_logging,
    sdbusplus,
  ]

inc_dir = include_directories('.')
libexecdir_installdir = join_paths(get_option('libexecdir'), 'phosphor-data-sync')
executable('phosphor-rbmc-data-sync-mgr',
    rbmc_data_sync_sources,
    dependencies: rbmc_data_sync_dependencies,
    include_directories: inc_dir,
    install : true,
    install_dir: libexecdir_installdir
  )
