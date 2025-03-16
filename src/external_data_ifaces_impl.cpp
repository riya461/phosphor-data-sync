// SPDX-License-Identifier: Apache-2.0

#include "external_data_ifaces_impl.hpp"

#include <xyz/openbmc_project/State/BMC/Redundancy/Sibling/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>

namespace data_sync::ext_data
{

ExternalDataIFacesImpl::ExternalDataIFacesImpl(sdbusplus::async::context& ctx) :
    _ctx(ctx)
{}

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFacesImpl::fetchBMCRedundancyMgrProps()
{
    // TODO Handle the exception and exit gracefully, as the data sync relies
    //      heavily on these DBus properties and cannot function effectively
    //      without them.
    //      Create error log?
    auto rbmcMgr = sdbusplus::async::proxy()
                       .service(RBMC::interface)
                       .path(RBMC::instance_path)
                       .interface(RBMC::interface);

    auto props =
        co_await rbmcMgr.get_all_properties<RBMC::PropertiesVariant>(_ctx);

    auto it = props.find("Role");
    if (it != props.end())
    {
        bmcRole(std::get<BMCRole>(it->second));
    }

    it = props.find("RedundancyEnabled");
    if (it != props.end())
    {
        bmcRedundancy(std::get<BMCRedundancy>(it->second));
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFacesImpl::fetchSiblingBmcPos()
{
    // TODO: Handle the exception and exit gracefully, as the data sync relies
    //      heavily on these DBus properties and cannot function effectively
    //      without them.

    using SiblingBMCMgr = sdbusplus::client::xyz::openbmc_project::state::bmc::
        redundancy::Sibling<>;

    using SiblingBMC = sdbusplus::common::xyz::openbmc_project::state::bmc::
        redundancy::Sibling;

    std::string siblingBMCInstancePath =
        std::string(SiblingBMC::namespace_path::value) + "/" +
        SiblingBMC::namespace_path::bmc;

    auto siblingBMCMgr = SiblingBMCMgr(_ctx)
                             .service(SiblingBMC::interface)
                             .path(siblingBMCInstancePath);

    siblingBmcPos(co_await siblingBMCMgr.bmc_position());

    co_return;
}

} // namespace data_sync::ext_data
