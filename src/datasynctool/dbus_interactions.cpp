// SPDX-License-Identifier: Apache-2.0

#include "dbus_interactions.hpp"

#include "utils.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/client.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/common.hpp>
#include <xyz/openbmc_project/Provisioning/Provisioning/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>

#include <iostream>
#include <print>
#include <variant>

namespace datasynctool::dbus_interactions
{

using SyncBMCData =
    sdbusplus::common::xyz::openbmc_project::control::SyncBMCData;

sdbusplus::async::task<> displayStatus(sdbusplus::async::context& ctx,
                                       bool jsonOutput)
{
    try
    {
        using SyncBMCDataMgr =
            sdbusplus::client::xyz::openbmc_project::control::SyncBMCData<>;
        using RedundancyMgr =
            sdbusplus::client::xyz::openbmc_project::state::bmc::Redundancy<>;
        using ProvisioningMgr = sdbusplus::client::xyz::openbmc_project::
            provisioning::Provisioning<>;

        json statusData;

        auto syncProps = co_await SyncBMCDataMgr(ctx)
                             .service(SyncBMCData::interface)
                             .path(SyncBMCData::instance_path)
                             .properties();

        statusData["Sync Enabled"] = !syncProps.disable_sync;
        statusData["Full Sync Status"] =
            utils::extractEnumValue(SyncBMCData::convertFullSyncStatusToString(
                syncProps.full_sync_status));
        statusData["Background Sync Status"] = utils::extractEnumValue(
            SyncBMCData::convertSyncEventsHealthToString(
                syncProps.sync_events_health));

        auto rbmcProps =
            co_await RedundancyMgr(ctx)
                .service("xyz.openbmc_project.State.BMC.Redundancy")
                .path("/xyz/openbmc_project/state/bmc0")
                .properties();

        statusData["Redundancy Enabled"] = rbmcProps.redundancy_enabled;
        statusData["Role"] = utils::extractEnumValue(
            RedundancyMgr::convertRoleToString(rbmcProps.role));

        try
        {
            auto peerConnected =
                co_await ProvisioningMgr(ctx)
                    .service("xyz.openbmc_project.Provisioning")
                    .path("/xyz/openbmc_project/Provisioning")
                    .peer_connected();

            statusData["Peer Connected"] = utils::extractEnumValue(
                ProvisioningMgr::convertPeerConnectionStatusToString(
                    peerConnected));
        }
        catch (const std::exception&)
        {}

        if (jsonOutput)
        {
            std::println("{}", statusData.dump(4));
        }
        else
        {
            utils::displayJsonAsText(statusData);
        }

        co_return;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading D-Bus properties: " << e.what() << "\n";
        throw;
    }
}

sdbusplus::async::task<> startFullSync(sdbusplus::async::context& ctx)
{
    try
    {
        using SyncBMCDataMgr =
            sdbusplus::client::xyz::openbmc_project::control::SyncBMCData<>;

        lg2::info("datasynctool attempting a full sync.");

        co_await SyncBMCDataMgr(ctx)
            .service(SyncBMCData::interface)
            .path(SyncBMCData::instance_path)
            .start_full_sync();

        std::println("Full sync initiated. See progress in journal logs");

        co_return;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error starting full sync: " << e.what() << "\n";
        throw;
    }
}

sdbusplus::async::task<> setSyncEnabled(sdbusplus::async::context& ctx,
                                        bool enable)
{
    try
    {
        using SyncBMCDataMgr =
            sdbusplus::client::xyz::openbmc_project::control::SyncBMCData<>;

        lg2::info("datasynctool trying to {ACTION} sync", "ACTION",
                  enable ? "enable" : "disable");

        co_await SyncBMCDataMgr(ctx)
            .service(SyncBMCData::interface)
            .path(SyncBMCData::instance_path)
            .disable_sync(!enable);

        std::println("Sync {} successfully", enable ? "enabled" : "disabled");

        co_return;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error setting sync state: " << e.what() << "\n";
        throw;
    }
}

} // namespace datasynctool::dbus_interactions
