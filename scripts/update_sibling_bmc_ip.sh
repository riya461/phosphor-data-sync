#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

# This script updates the sibling BMC IP in the corresponding data sync stunnel
# configuration file based on the sibling BMC's position.

set -e

localBMCPos=-1
siblingBMCPos=-1
siblingBMCIPAddr=""

getSiblingBMCPosition()
{
    # The awk command is used to extract the object path from
    # the GetSubTreePaths response, which is an array of object paths (as).
    # For example: as 1 "/xyz/openbmc_project/state/bmc1". It is also used
    # to retrieve the "BMCPosition" property value, such as "u 0".
    siblingBMCObjPath=$(busctl call xyz.openbmc_project.ObjectMapper \
            /xyz/openbmc_project/object_mapper xyz.openbmc_project.ObjectMapper \
            GetSubTreePaths sias / 0 \
            1 xyz.openbmc_project.State.BMC.Redundancy.Sibling | \
        awk '{split($0, arr, " "); gsub(/"/, "", arr[3]); print arr[3]}')
    siblingBMCService=$(mapper get-service "$siblingBMCObjPath")
    siblingBMCPos=$(busctl get-property "$siblingBMCService" \
            "$siblingBMCObjPath" xyz.openbmc_project.State.BMC.Redundancy.Sibling \
        BMCPosition | awk '{split($0, arr, " "); print arr[2]}');
}

getLocalBMCPosition()
{
    # Note: It's temporary until the corresponding daemon hosts
    #       the local BMC position.
    localBMCPos=$(fw_printenv -n bmc_position)
}

getSiblingBMCIPAddr()
{
    # Note: It's temporary until the networkd hosts the sibling BMC IP address.
    if [ "$1" = 0 ]
    then
        # Using the simics bmc0 eth0 IP address
        siblingBMCIPAddr="10.0.2.100"
    elif [ "$1" = 1 ]
    then
        # Using the simics bmc1 eth0 IP address
        siblingBMCIPAddr="10.2.2.100"
    else
        echo "Unsupported [$1] sibling bmc position"
        exit 1
    fi
}

getLocalBMCPosition
getSiblingBMCPosition
getSiblingBMCIPAddr "$siblingBMCPos"

# The below sed command modifies the first "connect = " line inside
# the "[sibling_bmc]" service section, replacing the IP address and stopping
# further modifications after that. It also stops processing when encountering
# a new service section ([something]).
# In the stunnel configuration, each service section begins with "[" and
# continues until another section header ("[") is found.
sed -i '/sibling_bmc/ {n; :loop; /^\[/ b end; \
        /connect = / {s/\(connect = \)[^:]*\(.*\)/\1'$siblingBMCIPAddr'\2/; \
    b end;}; n; b loop; :end;}' \
    /etc/phosphor-data-sync/stunnel/bmc"$localBMCPos"_stunnel.conf
