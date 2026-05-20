# SPDX-License-Identifier: Apache-2.0

# CLI11 dependency for datasynctool
cli11_dep = dependency('CLI11')

# Build datasynctool executable

datasynctool_sources = files('dbus_interactions.cpp', 'main.cpp', 'utils.cpp')

datasynctool_dependencies = [
    cli11_dep,
    sdbusplus_dep,
    phosphor_dbus_interfaces_dep,
    phosphor_logging_dep,
    nlohmann_json_dep,
]

executable(
    'datasynctool',
    datasynctool_sources,
    dependencies: datasynctool_dependencies,
    include_directories: include_directories('.'),
    install: true,
    install_dir: get_option('bindir'),
)
