#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

# This script generates temporary certificates and a private key for BMCs.
# These are used during data synchronization.

# Note: This is a temporary workaround. Ideally, the certificates and
# private key should be generated and stored in the BMC-specific TPM during
# provision, but that support is not yet available.

set -eu

if [ $# -ne 1 ] ; then
    echo "Usage: $(basename "$0") <path_to_store_generated_files>"
    exit 1
fi

# Argument
PATH_TO_STORE="$1"

# Config
CA_DIR="$PATH_TO_STORE"
CERTS_DIR="$PATH_TO_STORE"
DAYS_VALID=365

rm -rf "$CA_DIR" "$CERTS_DIR"
mkdir -p "$CA_DIR" "$CERTS_DIR"

# 1. Generate CA
openssl genrsa -out "$CA_DIR"/ca.key 2048 > /dev/null 2>&1
openssl req -x509 -new -nodes -key "$CA_DIR"/ca.key -sha256 -days "$DAYS_VALID" -out "$CA_DIR"/ca.crt -subj "/C=IN/ST=KA/L=Bangalore/O=IBM/OU=ISDL/CN=rbmc/emailAddress=powerfw@in.ibm.com" > /dev/null 2>&1

# Function to generate certs
generate_cert() {
    NAME=$1
    openssl genrsa -out "$CERTS_DIR"/"$NAME".key 2048 > /dev/null 2>&1
    openssl req -new -key "$CERTS_DIR"/"$NAME".key -out "$CERTS_DIR"/"$NAME".csr -subj "/C=IN/ST=KA/L=Bangalore/O=IBM/OU=ISDL/CN=$NAME/emailAddress=powerfw@in.ibm.com" > /dev/null 2>&1
    openssl x509 -req -in "$CERTS_DIR"/"$NAME".csr -CA "$CA_DIR"/ca.crt -CAkey "$CA_DIR"/ca.key -CAcreateserial -out "$CERTS_DIR"/"$NAME".crt -days "$DAYS_VALID" -sha256 > /dev/null 2>&1
}

# 2. Generate bmc0 and bmc1 certs
generate_cert bmc0
generate_cert bmc1

# Remove unwanted files
rm -rf "$CA_DIR"/ca.key
rm -rf "$CA_DIR"/ca.srl
rm -rf "$CERTS_DIR"/bmc0.csr
rm -rf "$CERTS_DIR"/bmc1.csr
