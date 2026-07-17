// SPDX-License-Identifier: Apache-2.0

#include "external_data_ifaces_impl.hpp"

#include "error_log.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Logging/Create/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>

#include <fstream>
#include <limits>

namespace data_sync::ext_data
{

constexpr auto bmcPositionFile = "/run/openbmc/bmc_position";

ExternalDataIFacesImpl::ExternalDataIFacesImpl(sdbusplus::async::context& ctx) :
    _ctx(ctx)
{}

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
    std::ifstream posFile(bmcPositionFile);
    if (!posFile.is_open())
    {
        throw std::runtime_error(std::string("Cannot open ") + bmcPositionFile);
    }

    BMCPosition pos{};
    // max<size_t>() indicates that the BMC position could not be determined.
    if (!(posFile >> pos) || pos == std::numeric_limits<BMCPosition>::max())
    {
        throw std::runtime_error(std::string("Invalid BMC position in ") +
                                 bmcPositionFile);
    }

    bmcPosition(pos);
    lg2::debug("BMC position read from file: {POSITION}", "POSITION", pos);

    co_return;
}

sdbusplus::async::task<> ExternalDataIFacesImpl::createErrorLog(
    const std::string& errMsg, const ErrorLevel& errSeverity,
    AdditionalData& additionalDetails,
    const std::optional<json>& calloutsDetails)
{
    try
    {
        error_log::FFDCFileInfoSet ffdcFileInfoSet;
        if (calloutsDetails.has_value())
        {
            error_log::FFDCFile file(error_log::FFDCFormat::JSON, 0xCA, 0x01,
                                     calloutsDetails.value().dump());
            ffdcFileInfoSet.emplace_back(file.getFormat(), file.getSubType(),
                                         file.getVersion(), file.getFD());
        }

        additionalDetails["_PID"] = std::to_string(getpid());

        using LoggingProxy =
            sdbusplus::client::xyz::openbmc_project::logging::Create<>;

        co_await LoggingProxy(_ctx)
            .service(Logging::default_service)
            .path(Logging::instance_path)
            .create_with_ffdc_files(errMsg, errSeverity, additionalDetails,
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
