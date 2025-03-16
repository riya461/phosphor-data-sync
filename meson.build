# SPDX-License-Identifier: Apache-2.0

project(
    'rbmc_data_sync',
    'cpp',
    'c',
    default_options: [
        'buildtype=debugoptimized',
        'warning_level=3',
        'werror=true',
        'cpp_std=c++23',
    ],
    meson_version: '>=1.3.0',
    version: '1.0.0',
)

data_sync_config_dir = get_option('datadir') + '/phosphor-data-sync/config/data_sync_list/'

foreach json_file_name : get_option('data_sync_list')

    #check whether the json file given in the list exist and if so, install the same
    json_file = files('config/data_sync_list/' + json_file_name + '.json')

    install_data(json_file, install_dir: data_sync_config_dir)

endforeach

# auto generate a config file with required build time configurations
conf_data = configuration_data()

# TODO : Modify the meson cross compilation setup to override $prefix as '/usr'
# and use $prefix instead of hardcoding '/usr'.
conf_data.set_quoted(
    'DATA_SYNC_CONFIG_DIR',
    '/usr/' + data_sync_config_dir,
    description: 'Path where the JSON config files resides',
)
conf_data.set(
    'DEFAULT_RETRY_ATTEMPTS',
    get_option('retry_attempts'),
    description: 'Default retry attempts for all data to be synced',
)
conf_data.set(
    'DEFAULT_RETRY_INTERVAL',
    get_option('retry_interval'),
    description: 'Default retry interval for all data to be synced',
)

conf_h_dep = declare_dependency(
    include_directories: include_directories('.'),
    sources: configure_file(output: 'config.h', configuration: conf_data),
)

subdir('src')
subdir('config')

if not get_option('tests').disabled()
    subdir('test')
endif
