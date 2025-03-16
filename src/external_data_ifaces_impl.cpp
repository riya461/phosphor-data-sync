// SPDX-License-Identifier: Apache-2.0

#include "external_data_ifaces_impl.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Position/client.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>

namespace data_sync::ext_data
{

ExternalDataIFacesImpl::ExternalDataIFacesImpl(sdbusplus::async::context& ctx) :
    _ctx(ctx)
{}

sdbusplus::async::task<std::string>
    // NOLINTNEXTLINE
    ExternalDataIFacesImpl::getDBusService(const std::string& objPath,
                                           const std::string& interface)
{
    try
    {
        using ObjectMapperMgr =
            sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

        auto objectMapperMgr = ObjectMapperMgr(_ctx)
                                   .service(ObjectMapperMgr::default_service)
                                   .path(ObjectMapperMgr::instance_path);

        std::vector<std::string> interfaces{interface};

        auto services = co_await objectMapperMgr.get_object(objPath,
                                                            interfaces);

        co_return services.begin()->first;
    }
    catch (const std::exception& e)
    {
        lg2::error("D-Bus error [{ERROR}] while trying to get service name for "
                   "ObjectPath: {OBJ_PATH} Interface: {IFACE}",
                   "ERROR", e, "OBJ_PATH", objPath, "IFACE", interface);
        throw;
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFacesImpl::fetchBMCRedundancyMgrProps()
{
    try
    {
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
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to get the RBMC properties, error: {ERROR}", "ERROR",
                   e);
        throw;
    }
    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFacesImpl::fetchBMCPosition()
{
    try
    {
        // In a redundant BMC system, the local BMC position is maintained
        // in the system inventory.
        using PositionMgr = sdbusplus::client::xyz::openbmc_project::inventory::
            decorator::Position<>;

        const auto* const systemInvObjPath =
            "/xyz/openbmc_project/inventory/system";

        auto service = co_await getDBusService(systemInvObjPath,
                                               PositionMgr::interface);

        bmcPosition(co_await PositionMgr(_ctx)
                        .service(service)
                        .path(systemInvObjPath)
                        .position());
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to get the BMC position, error: {ERROR}", "ERROR",
                   e);
        throw;
    }
    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ExternalDataIFacesImpl::fetchRbmcCredentials()
{
    // TODO: Currently, the username and password for BMCs are hardcoded.
    // Once user management DBus exposes the username and the encrypted password
    // available, update the logic to retrieve them dynamically.

    // here, username hardcode as service and password as 0penBmc0
    rbmcCredentials(std::make_pair("service", "0penBmc0"));
    co_return;
}

} // namespace data_sync::ext_data
