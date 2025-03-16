# SPDX-License-Identifier: Apache-2.0
#!/bin/sh

# This script generates rsync and stunnel configs for BMC0 and BMC1
# from templates, replacing placeholders using the sync socket
# configuration files.

set -eu

if [ "$#" -lt 4 ]; then
    echo "Usage: $0 <input_cfg_dir> <output_cfg_dir> <sync_sock_cfg_dir> <sync_sock_cfg_prefix1> [<sync_sock_cfg_prefix2> ...]"
    exit 1
fi

IN_CFG_DIR="$1"
OUT_CFG_DIR="$2"
SYNC_SOCKET_DIR="$3"
shift 3 # Sync socket configuration files base name and $@ will be used in below

RSYNC_TEMPLATE="$IN_CFG_DIR/rsyncd.conf.in"

CERT_DIR="/etc/phosphor-data-sync/certs"
RSYNC_OUT_DIR="$OUT_CFG_DIR/rsync"

# Check template files exist
for f in "$RSYNC_TEMPLATE"; do
    if [ ! -f "$f" ]; then
        echo "Missing required file: $f" >&2
        exit 1
    fi
done

mkdir -p "$RSYNC_OUT_DIR"

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
for base in "$@"; do
    cfg_path="$SYNC_SOCKET_DIR/${base}_sync_socket.cfg"
    if [ ! -f "$cfg_path" ]; then
        continue
    fi
    while IFS='=' read -r key value; do
        # Skip comments and empty lines
        case "$key" in
            ''|\#*) continue ;;
        esac
        # Trim whitespace
        key=$(echo "$key" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        value=$(echo "$value" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        eval "$key='$value'"
        eval "VAR_SRC_$key='$base'"
    done < "$cfg_path"
done

# Required variables
required_vars="BMC0_RSYNC_PORT BMC1_RSYNC_PORT"

# Validate required variables
missing_vars=""
for var in $required_vars; do
    eval "val=\${$var-}"
    if [ -z "$val" ]; then
        missing_vars="$missing_vars $var"
    fi
done

if [ -n "$missing_vars" ]; then
    echo "Missing required variables:$missing_vars"
    exit 1
fi

# Print configuration summary
echo "Configuration:"
for var in $required_vars; do
    eval "val=\${$var}"
    eval "src=\${VAR_SRC_$var:-unknown}"
    printf "%-20s = %-15s (%s)\n" "$var" "$val" "$src"
done
echo

# Convert string to lowercase
tolower() {
    echo "$1" | tr '[:upper:]' '[:lower:]'
}

# Config generation function
generate_conf() {
    bmc="$1"
    if [ "$bmc" = "BMC0" ]; then
        sib="BMC1"
    else
        sib="BMC0"
    fi

    lbmc=$(tolower "$bmc")
    lsib=$(tolower "$sib")

    eval RSYNC_PORT=\$${bmc}_RSYNC_PORT

    RSYNC_OUT="$RSYNC_OUT_DIR/${lbmc}_rsyncd.conf"

    sed "s|<BMC_RSYNC_PORT>|$RSYNC_PORT|g" "$RSYNC_TEMPLATE" > "$RSYNC_OUT"
}

# Generate rsync configuration files for both BMCs
generate_conf "BMC0"
generate_conf "BMC1"
