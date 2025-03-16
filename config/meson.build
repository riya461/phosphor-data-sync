# SPDX-License-Identifier: Apache-2.0

foreach json_file_name : get_option('data_sync_list')

    #check whether the json file given in the list exist and if so, install the same
    json_file = files('data_sync_list/' + json_file_name + '.json')

    install_data(json_file, install_dir: data_sync_config_dir)

endforeach
