// SPDX-License-Identifier: Apache-2.0

#include "external_data_ifaces_impl.hpp"

#include "error_log.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Position/client.hpp>
#include <xyz/openbmc_project/Logging/Create/client.hpp>
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
        using RedundancyMgr =
            sdbusplus::client::xyz::openbmc_project::state::bmc::Redundancy<>;

        auto rbmcMgrProps = co_await RedundancyMgr(_ctx)
                                .service(RBMC::interface)
                                .path(RBMC::instance_path)
                                .properties();

        bmcRole(rbmcMgrProps.role);
        bmcRedundancy(rbmcMgrProps.redundancy_enabled);
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

        // NOLINTNEXTLINE
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

sdbusplus::async::task<>
    ExternalDataIFacesImpl::createErrorLog(const std::string& errMsg,
                                           const ErrorLevel& errSeverity,
                                           const json& calloutsDetails)
{
    try
    {
        error_log::FFDCFileInfoSet ffdcFileInfoSet;
        if (!calloutsDetails.is_null())
        {
            error_log::FFDCFile file(error_log::FFDCFormat::JSON, 0xCA, 0x01,
                                     calloutsDetails.dump());
            ffdcFileInfoSet.emplace_back(file.getFormat(), file.getSubType(),
                                         file.getVersion(), file.getFD());
        }

        std::map<std::string, std::string> additionalData;
        additionalData.emplace("_PID", std::to_string(getpid()));

        using LoggingProxy =
            sdbusplus::client::xyz::openbmc_project::logging::Create<>;

        co_await LoggingProxy(_ctx)
            .service(Logging::default_service)
            .path(Logging::instance_path)
            .create_with_ffdc_files(errMsg, errSeverity, additionalData,
                                    ffdcFileInfoSet);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to create error log for {ERR_MSG}, error: {ERROR}",
                   "ERR_MSG", errMsg, "ERROR", e);
    }
    co_return;
}

sdbusplus::async::task<bool> ExternalDataIFacesImpl::systemdServiceAction(
    const std::string& service, const std::string& systemdMethod)
{
    try
    {
        auto systemdReload = sdbusplus::async::proxy()
                                 .service("org.freedesktop.systemd1")
                                 .path("/org/freedesktop/systemd1")
                                 .interface("org.freedesktop.systemd1.Manager");

        using objectPath = sdbusplus::message::object_path;
        lg2::info("Requesting systemd to {METHOD}:{SERVICE} due to data update",
                  "METHOD", systemdMethod, "SERVICE", service);
        co_await systemdReload.call<objectPath>(_ctx, systemdMethod, service,
                                                "replace");

        co_return true;
    }
    catch (const std::exception& e)
    {
        lg2::error("DBus call to {METHOD}:{SERVICE} failed, Exception: {EXCEP}",
                   "METHOD", systemdMethod, "SERVICE", service, "EXCEP", e);
        co_return false;
    }
}

sdbusplus::async::task<> ExternalDataIFacesImpl::watchRedundancyMgrProps()
{
    sdbusplus::async::match match(
        _ctx, sdbusplus::bus::match::rules::propertiesChanged(
                  RBMC::instance_path, RBMC::interface));

    using PropertyMap = std::map<std::string, RBMC::PropertiesVariant>;

    while (!_ctx.stop_requested())
    {
        auto [_, props] = co_await match.next<std::string, PropertyMap>();

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
    co_return;
}

} // namespace data_sync::ext_data
