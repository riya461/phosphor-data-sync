// SPDX-License-Identifier: Apache-2.0

#include "external_data_ifaces.hpp"

namespace data_sync::ext_data
{

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFaces::startExtDataFetches()
{
    // NOLINTNEXTLINE
    co_return co_await sdbusplus::async::execution::when_all(
        fetchBMCRedundancyMgrProps(), fetchSiblingBmcPos(),
        fetchRbmcCredentials());
}

BMCRole ExternalDataIFaces::bmcRole() const
{
    return _bmcRole;
}

void ExternalDataIFaces::bmcRole(const BMCRole& bmcRole)
{
    _bmcRole = bmcRole;
}

BMCRedundancy ExternalDataIFaces::bmcRedundancy() const
{
    return _bmcRedundancy;
}

void ExternalDataIFaces::bmcRedundancy(const BMCRedundancy& bmcRedundancy)
{
    _bmcRedundancy = bmcRedundancy;
}

const SiblingBmcPos& ExternalDataIFaces::siblingBmcPos() const
{
    return _siblingBmcPos;
}

void ExternalDataIFaces::siblingBmcPos(const SiblingBmcPos& siblingBmcPos)
{
    _siblingBmcPos = siblingBmcPos;
}

void ExternalDataIFaces::rbmcCredentials(const RbmcCredentials& rbmcCredentials)
{
    _rbmcCredentials = rbmcCredentials;
}

const RbmcCredentials& ExternalDataIFaces::rbmcCredentials() const
{
    return _rbmcCredentials;
}
} // namespace data_sync::ext_data
