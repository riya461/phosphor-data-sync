// SPDX-License-Identifier: Apache-2.0

#include "external_data_ifaces_impl.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/Sibling/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>

#include <filesystem>
#include <fstream>

namespace data_sync::ext_data
{
namespace fs = std::filesystem;

ExternalDataIFacesImpl::ExternalDataIFacesImpl(sdbusplus::async::context& ctx) :
    _ctx(ctx)
{}

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFacesImpl::fetchBMCRedundancyMgrProps()
{
#if 0
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
#else
    bmcRedundancy(true);
    bmcRole(BMCRole::Active);
    const std::string bmcRoleFPath("/tmp/pds/bmc_role");
    if (fs::exists(bmcRoleFPath))
    {
        std::ifstream file(bmcRoleFPath);
        if (file)
        {
            std::string bRole((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            if (bRole.contains("Passive"))
            {
                bmcRole(BMCRole::Passive);
            }
        }
    }
#endif
    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFacesImpl::fetchSiblingBmcPos()
{
#if 0
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
#else
    siblingBmcPos(1);
    const std::string siblingPosFPath("/tmp/pds/sibling_bmc_pos");
    if (fs::exists(siblingPosFPath))
    {
        std::ifstream file(siblingPosFPath);
        if (file)
        {
            std::string sBMCPos((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            if (sBMCPos.contains("0"))
            {
                siblingBmcPos(0);
            }
        }
    }

#endif

    co_return;
}

} // namespace data_sync::ext_data
