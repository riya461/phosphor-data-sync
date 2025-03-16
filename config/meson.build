# SPDX-License-Identifier: Apache-2.0

foreach json_file_name : get_option('data_sync_list')

    #check whether the json file given in the list exist and if so, install the same
    json_file = files('data_sync_list/' + json_file_name + '.json')

    install_data(json_file, install_dir: data_sync_config_dir)

endforeach

etc_dir = get_option('sysconfdir') + '/phosphor-data-sync/'

# Process rsync configuration templates to substitute fields
# defined by Meson
conf_files = ['rsync/rsyncd.conf.in']

conf_files_data = configuration_data()
conf_files_data.set('RSYNCD_MODULE_NAME', 'bmc_fs')

processed_templates_files = []
foreach conf_file : conf_files
    filename = conf_file.split('/')[-1]  # just filename
    configured_file = configure_file(
        input: conf_file,
        output: filename,
        configuration: conf_files_data,
    )
    processed_templates_files += configured_file
endforeach

# Use processed configuration templates and sync socket configurations to
# generate rsync files for each BMC in redundant systems
gen_cfg_files_path = join_paths(meson.current_build_dir(), 'gen_cfg_files')
custom_target(
    'generate_rsync_cfg',
    input: processed_templates_files,  # wait for processing template file
    output: ['dummy.txt'],
    command: [
        files(meson.project_source_root() / 'scripts/gen_rsync_cfg.sh'),
        meson.current_build_dir(),
        gen_cfg_files_path,
        join_paths(meson.current_source_dir(), 'sync_socket'),
        get_option('data_sync_list'),
    ],
    build_by_default: true,
)

# Install generated rsync configuration files
install_subdir(gen_cfg_files_path, install_dir: etc_dir, strip_directory: true)
install_data(
    'rsync/rsyncd_bmc_fs.filter',
    install_dir: etc_dir,
    preserve_path: true,
)
