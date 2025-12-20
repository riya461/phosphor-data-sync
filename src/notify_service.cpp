// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "notify_service.hpp"

#include "external_data_ifaces.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <experimental/scope>
#include <fstream>
#include <iostream>
#include <map>

namespace data_sync::notify
{
namespace file_operations
{
nlohmann::json readFromFile(const fs::path& notifyFilePath)
{
    std::ifstream notifyFile;
    notifyFile.open(fs::path(NOTIFY_SERVICES_DIR) / notifyFilePath);

    return nlohmann::json::parse(notifyFile);
}
} // namespace file_operations

NotifyService::NotifyService(
    sdbusplus::async::context& ctx,
    data_sync::ext_data::ExternalDataIFaces& extDataIfaces,
    const fs::path& notifyFilePath, CleanupCallback cleanup) :
    _ctx(ctx), _extDataIfaces(extDataIfaces), _cleanup(std::move(cleanup))
{
    _ctx.spawn(init(notifyFilePath));
}

sdbusplus::async::task<bool>
    NotifyService::sendSystemdNotification(const std::string& service,
                                           const std::string& systemdMethod)
{
    // retryAttempt = 0 indicates initial attempt, rest implies retries
    uint8_t retryAttempt = 0;

    while (retryAttempt++ <= DEFAULT_RETRY_ATTEMPTS)
    {
        bool success = co_await _extDataIfaces.systemdServiceAction(
            service, systemdMethod);

        if (success)
        {
            co_return true;
        }

        // No more retries left
        if (retryAttempt > DEFAULT_RETRY_ATTEMPTS)
        {
            break;
        }

        lg2::debug(
            "Scheduling retry[{ATTEMPT}/{MAX}] for {SERVICE} after {SEC}s",
            "ATTEMPT", retryAttempt, "MAX", DEFAULT_RETRY_ATTEMPTS, "SERVICE",
            service, "SEC", DEFAULT_RETRY_INTERVAL);

        co_await sleep_for(_ctx, std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    }

    lg2::error(
        "Failed to notify {SERVICE} via {METHOD} ; All {MAX_ATTEMPTS} retries "
        "exhausted",
        "SERVICE", service, "METHOD", systemdMethod, "MAX_ATTEMPTS",
        DEFAULT_RETRY_ATTEMPTS);

    co_return false;
}

sdbusplus::async::task<>
    NotifyService::systemdNotify(const nlohmann::json& notifyRqstJson)
{
    const auto services = notifyRqstJson["NotifyInfo"]["NotifyServices"]
                              .get<std::vector<std::string>>();
    const std::string& modifiedPath =
        notifyRqstJson["ModifiedDataPath"].get<std::string>();
    const std::string& systemdMethod =
        ((notifyRqstJson["NotifyInfo"]["Method"].get<std::string>()) == "Reload"
             ? "ReloadUnit"
             : "RestartUnit");

    for (const auto& service : services)
    {
        // Will notify each service sequentially assuming they are dependent
        bool result = co_await sendSystemdNotification(service, systemdMethod);

        // Create PEL if notify failed
        if (!result)
        {
            // TODO : Add additional info to the PEL
            co_await _extDataIfaces.createErrorLog(
                "xyz.openbmc_project.RBMC_DataSync.Error.NotifyFailure",
                ext_data::ErrorLevel::Informational, {});
        }
    }
    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> NotifyService::init(fs::path notifyFilePath)
{
    // Ensure cleanup is called when coroutine completes
    using std::experimental::scope_exit;
    auto cleanupGuard = scope_exit([this] {
        if (_cleanup)
        {
            _cleanup(this);
        }
    });

    nlohmann::json notifyRqstJson{};
    try
    {
        notifyRqstJson = file_operations::readFromFile(notifyFilePath);
    }
    catch (const std::exception& exc)
    {
        lg2::error(
            "Failed to read the notify request file[{FILEPATH}], Error : {ERR}",
            "FILEPATH", notifyFilePath, "ERR", exc);
        throw std::runtime_error("Failed to read the notify request file");
    }
    if (notifyRqstJson["NotifyInfo"]["Mode"] == "DBus")
    {
        // TODO : Implement DBus notification method
        lg2::warning(
            "Unable to process the notify request[{PATH}], as DBus mode is"
            " not available!!!. Received rqst : {RQSTJSON}",
            "PATH", notifyFilePath, "RQSTJSON",
            nlohmann::to_string(notifyRqstJson));
    }
    else if ((notifyRqstJson["NotifyInfo"]["Mode"] == "Systemd"))
    {
        co_await systemdNotify(notifyRqstJson);
    }
    else
    {
        lg2::error(
            "Notify failed due to unknown Mode in notify request[{PATH}], "
            "Request : {RQSTJSON}",
            "PATH", notifyFilePath, "RQSTJSON",
            nlohmann::to_string(notifyRqstJson));
    }

    try
    {
        fs::remove(notifyFilePath);
    }
    catch (const std::exception& exc)
    {
        lg2::error("Failed to remove notify file[{PATH}], Error: {ERR}", "PATH",
                   notifyFilePath, "ERR", exc);
    }

    co_return;
}

} // namespace data_sync::notify
