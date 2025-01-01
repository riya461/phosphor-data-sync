// SPDX-License-Identifier: Apache-2.0

#include "external_data_ifaces.hpp"

namespace data_sync::ext_data
{

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFaces::startExtDataFetches()
{
    // NOLINTNEXTLINE
    co_return co_await fetchBMCRedundancyMgrProps();
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

} // namespace data_sync::ext_data
