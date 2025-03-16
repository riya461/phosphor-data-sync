# SPDX-License-Identifier: Apache-2.0

foreach json_file_name : get_option('data_sync_list')

    #check whether the json file given in the list exist and if so, install the same
    json_file = files('data_sync_list/' + json_file_name + '.json')

    install_data(json_file, install_dir: data_sync_config_dir)

endforeach

etc_dir = get_option('sysconfdir') + '/phosphor-data-sync/'

rsync_conf_files = ['rsync/bmc0_rsyncd.conf', 'rsync/bmc1_rsyncd.conf']

rsync_conf_data = configuration_data()

rsync_conf_data.set('LOCAL_HOST_IP', '127.0.0.1')
rsync_conf_data.set('BMC0_RSYNC_PORT', 1703)
rsync_conf_data.set('BMC1_RSYNC_PORT', 1704)

foreach conf_file : rsync_conf_files
    configure_file(
        input: conf_file + '.in',
        output: '@BASENAME@',
        configuration: rsync_conf_data,
        install_dir: etc_dir + conf_file.split('/')[0],
    )
endforeach
