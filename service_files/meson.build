# SPDX-License-Identifier: Apache-2.0

service_files = [
    'xyz.openbmc_project.Control.SyncBMCData.service',
    'SyncBMCData_rsync.service',
]

systemd_system_unit_dir = dependency('systemd').get_variable(
    pkgconfig: 'systemdsystemunitdir',
)
fs = import('fs')
foreach service_file : service_files
    fs.copyfile(
        service_file,
        install: true,
        install_dir: systemd_system_unit_dir,
    )
endforeach
