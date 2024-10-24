# Data sync JSON configuration files

The [JSON files](data_sync_list) specify the files and directories to be
synchronized between the BMCs. Each configuration file must follow the schema
defined in the [`config/schema/schema.json`](schema/schema.json).

## Adding Files/Directories to Sync

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

### Add a new config JSON file

- If another vendor wants to add a new config JSON file into
  [`config/data_sync_list`](data_sync_list) with their list of data to be
  synced, define the schema compliant JSON file as `<vendor_name>.json`.
- Update the `choices` field of `data_sync_list` option in
  [meson.options](../meson.options) with the JSON file name without .json file
  extension.
