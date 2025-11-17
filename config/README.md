# Configuration

## Data sync JSON configuration files

The [JSON files](data_sync_list) specify the files and directories to be
synchronized between the BMCs. Each configuration file must follow the schema
defined in the [`config/schema/schema.json`](schema/schema.json).

### Adding Files/Directories to Sync

Users can either add files or directories for synchronization to one of the
existing JSON files, if they fit in the categories described below, or create a
new JSON file by following the schema in
[`config/schema/schema.json`](schema/schema.json).

Currently 3 JSON config files are defined under
[`config/data_sync_list`](data_sync_list) as below.

1. common.json - Contains files and directories of applications whic run across
   all OpenBMC vendors.
2. open-power.json - Contains files and directories of Open-Power applications.
3. ibm.json - Contains files and directories specific to applications on IBM
   systems.

#### Add a new config JSON file

- If another vendor wants to add a new config JSON file into
  [`config/data_sync_list`](data_sync_list) with their list of data to be
  synced, define the schema compliant JSON file as `<vendor_name>.json`.
- Update the `choices` field of `data_sync_list` option in
  [meson.options](../meson.options) with the JSON file name without .json file
  extension.

## Rsync and Stunnel Configuration Generation

The data sync application uses rsync and stunnel to securely synchronize data,
and both components require configuration files with the necessary details. For
redundant BMC systems (BMC0 and BMC1), these configuration files are generated
at build time using `Meson` along with
[a helper shell script](../scripts/gen_rsync_stunnel_cfg.sh).

### Configuration file generation flow

- The build extracts the necessary configuration from the
  [sync socket configuration file](sync_socket) as per `Meson` build option
  `data_sync_list`. For example, `-Ddata_sync_list=common,<vendor>`, the last
  entry overrides earlier ones (vendor overrides common).

```sh
config/sync_socket/common_sync_socket.cfg
config/sync_socket/<vendor>_sync_socket.cfg
```

- Verify that the extracted configuration parameters match the required
  parameters listed below; otherwise, the process will fail.

```sh
BMC0_RSYNC_PORT
BMC1_RSYNC_PORT
BMC0_STUNNEL_PORT
BMC1_STUNNEL_PORT
BMC0_IP
BMC1_IP
```

- These extracted configuration parameters are substituted into the placeholders
  within the [rsync](rsync) and [stunnel](stunnel) configuration template files,
  producing the required BMC-specific rsync and stunnel configuration files,
  which are installed as shown below. The same configuration values are also
  used to generate the `config.h` file, enabling those parameters to be applied
  in the rsync CLI.

```sh
/etc/phosphor-data-sync/rsync/bmc0_rsyncd.conf
/etc/phosphor-data-sync/rsync/bmc1_rsyncd.conf
/etc/phosphor-data-sync/stunnel/bmc0_stunnel.conf
/etc/phosphor-data-sync/stunnel/bmc1_stunnel.conf
```

- These generated configuration file will be picked as per the local BMC's
  position. Refer [rsync and stunnel service files](../service_files).

**Note:** The data sync supports static IP addresses for syncing. The vendor is
expected to config their own static IP in the sync socket configuration file.

### How to add a new vendor sync socket config file

1. Create a new file `config/sync_socket/<vendor>_sync_socket.cfg`.
1. Build with `-Ddata_sync_list=common,<vendor>`.

**Note:** The vendor configuration override values in common.
