#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

/usr/bin/python3 scripts/validate_data_sync_list.py \
    -s config/schema/schema.json \
    -f config/data_sync_list/*
