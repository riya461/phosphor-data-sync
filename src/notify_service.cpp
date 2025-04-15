// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "notify_service.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

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
    const fs::path& notifyFilePath) : _ctx(ctx), _extDataIfaces(extDataIfaces)
{
    _ctx.spawn(init(notifyFilePath));
}

// NOLINTNEXTLINE
sdbusplus::async::task<>
    NotifyService::sendSystemDNotification(const nlohmann::json& notifyRqstJson)
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
        try
        {
            co_await _extDataIfaces.systemDServiceAction(service,
                                                         systemdMethod);
        }
        catch (const std::exception& e)
        {
            lg2::error(
                "Notify request to {METHOD}:{SERVICE} failed; triggered as "
                "path[{PATH}] updated. Error: {ERROR}",
                "METHOD", systemdMethod, "SERVICE", service, "PATH",
                modifiedPath, "ERROR", e.what());
        }
    }
    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> NotifyService::init(fs::path notifyFilePath)
{
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
        // TODO : Implement DBUS notification method
        lg2::warning(
            "Unable to process the notify request[{PATH}], as DBus mode is"
            " not available!!!. Received rqst : {RQSTJSON}",
            "PATH", notifyFilePath, "RQSTJSON",
            nlohmann::to_string(notifyRqstJson));
    }
    else if ((notifyRqstJson["NotifyInfo"]["Mode"] == "Systemd"))
    {
        co_await sendSystemDNotification(notifyRqstJson);
    }
    else
    {
        lg2::error(
            "Failed to process the notify request[{PATH}], Request : {RQSTJSON}",
            "PATH", notifyFilePath, "RQSTJSON",
            nlohmann::to_string(notifyRqstJson));
        co_return;
    }
    fs::remove(notifyFilePath);

    co_return;
}

} // namespace data_sync::notify
