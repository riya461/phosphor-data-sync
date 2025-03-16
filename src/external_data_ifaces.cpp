// SPDX-License-Identifier: Apache-2.0

#include "external_data_ifaces.hpp"

namespace data_sync::ext_data
{

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFaces::startExtDataFetches()
{
    // NOLINTNEXTLINE
    co_return co_await sdbusplus::async::execution::when_all(
        fetchBMCRedundancyMgrProps(), fetchBMCPosition());
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

const BMCPosition& ExternalDataIFaces::bmcPosition() const
{
    return _bmcPosition;
}

void ExternalDataIFaces::bmcPosition(const BMCPosition& bmcPosition)
{
    _bmcPosition = bmcPosition;
}

} // namespace data_sync::ext_data
