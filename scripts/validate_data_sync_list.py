# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import sys

import jsonschema

r"""
The script validate JSON files which lists the files and directories to be
synced between the active and passive BMC against the defined schema by using
jsonschema module.

"""


def validate_schema(data_sync_list, schema_file):
    """API to validate the JSON config files against schema.json

    Args:
        data_sync_list : List of JSON config files
        schema_file : Path of schema file

    Returns: None
    """

    try:
        with open(schema_file) as schema_handle:
            schema_json = json.load(schema_handle)

        for config_file in data_sync_list:
            try:
                with open(config_file) as config_file_handle:
                    config_file_json = json.load(config_file_handle)
                    jsonschema.validators.validate(
                        config_file_json,
                        schema_json,
                        format_checker=jsonschema.Draft202012Validator.FORMAT_CHECKER,
                    )
                    print("Schema validation success for " + config_file)
            except Exception as error:
                sys.exit(
                    "Schema validation failed for "
                    + config_file
                    + "!!! Error : "
                    + str(error)
                )
    except Exception as error:
        sys.exit("Error in schema.json : " + str(error))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Data sync json config file validator"
    )

    parser.add_argument(
        "-s",
        "--schema",
        dest="schema_file",
        help="The data sync config JSON's schema file",
        required=True,
    )

    parser.add_argument(
        "-f",
        "--json_files",
        nargs="+",
        dest="data_sync_list",
        help="The data sync JSON config files",
        required=True,
    )

    args = parser.parse_args()

    validate_schema(args.data_sync_list, args.schema_file)
