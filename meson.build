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
rsyncd_module_name = 'bmc_fs'
bmc0_rsync_port = ''
bmc1_rsync_port = ''

# Directory used to store files containing sibling notification requests.
if get_option('tests').enabled()
    notify_sibling = '/tmp/phosphor-data-sync/notify-sibling-test/'
    notify_services = '/tmp/phosphor-data-sync/notify-services-test/'
else
    notify_sibling = get_option('localstatedir') + '/lib/phosphor-data-sync/notify-sibling/'
    notify_services = get_option('localstatedir') + '/lib/phosphor-data-sync/notify-services/'
endif

foreach name : get_option('data_sync_list')

    #check whether the json file given in the list exist and if so, install the same
    json_file = files('config/data_sync_list/' + name + '.json')

    install_data(json_file, install_dir: data_sync_config_dir)

    # Read configuration fields from each sync socket configuration file
    # in the order specified by the "data_sync_list" option.
    # Example:
    #   Option: -Ddata_sync_list=common,<vendor>
    #   Sync socket config files:
    #     config/sync_socket/common_sync_socket.cfg
    #     config/sync_socket/<vendor>_sync_socket.cfg
    # Note: The vendor name always appears last in the "data_sync_list" option
    #       (as defined by the OpenBMC meta layer) to allow vendor-specific
    #       parameters to override common configuration values.
    ss_cfg_file = join_paths(
        meson.project_source_root(),
        'config/sync_socket/' + name + '_sync_socket.cfg',
    )
    bmc0_rsync_port_t = run_command(
        'bash',
        '-c',
        'if [ -f "' + ss_cfg_file + '" ]; then grep "^BMC0_RSYNC_PORT=" "' + ss_cfg_file + '" | cut -d"=" -f2; fi',
    ).stdout().strip()
    if bmc0_rsync_port_t != ''
        bmc0_rsync_port = bmc0_rsync_port_t
    endif
    bmc1_rsync_port_t = run_command(
        'bash',
        '-c',
        'if [ -f "' + ss_cfg_file + '" ]; then grep "^BMC1_RSYNC_PORT=" "' + ss_cfg_file + '" | cut -d"=" -f2; fi',
    ).stdout().strip()
    if bmc1_rsync_port_t != ''
        bmc1_rsync_port = bmc1_rsync_port_t
    endif
endforeach
# Ensure ports are set
if bmc0_rsync_port == '' or bmc1_rsync_port == ''
    error(
        'BMC0_RSYNC_PORT or BMC1_RSYNC_PORT not defined in any sync socket file',
    )
endif

# auto generate a config file with required build time configurations
conf_data = configuration_data()

# TODO : Modify the meson cross compilation setup to override $prefix as '/usr'
# and use $prefix instead of hardcoding '/usr'.
conf_data.set_quoted(
    'DATA_SYNC_CONFIG_DIR',
    '/usr/' + data_sync_config_dir,
    description: 'Path where the JSON config files resides',
)
conf_data.set_quoted(
    'NOTIFY_SIBLING_DIR',
    notify_sibling,
    description: 'Directory where sibling notify requests get created',
)
conf_data.set_quoted(
    'NOTIFY_SERVICES_DIR',
    notify_services,
    description: 'Directory which receives the notify requests from sibling BMC',
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
conf_data.set_quoted(
    'RSYNCD_MODULE_NAME',
    rsyncd_module_name,
    description: 'Rsync daemon configured module name',
)
conf_data.set_quoted(
    'BMC0_RSYNC_PORT',
    bmc0_rsync_port,
    description: 'BMC0 rsyncd port',
)
conf_data.set_quoted(
    'BMC1_RSYNC_PORT',
    bmc1_rsync_port,
    description: 'BMC1 rsyncd port',
)

conf_h_dep = declare_dependency(
    include_directories: include_directories('.'),
    sources: configure_file(output: 'config.h', configuration: conf_data),
)

subdir('src')

if get_option('tests').enabled()
    subdir('test')
else
    subdir('config')
    subdir('scripts')
    subdir('service_files')
endif
