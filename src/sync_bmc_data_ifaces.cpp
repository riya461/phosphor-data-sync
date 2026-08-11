// SPDX-License-Identifier: Apache-2.0

#include "sync_bmc_data_ifaces.hpp"

#include "manager.hpp"

#include <phosphor-logging/lg2.hpp>

namespace data_sync::dbus_ifaces
{

using SyncBMCData =
    sdbusplus::common::xyz::openbmc_project::control::SyncBMCData;

SyncBMCDataIface::SyncBMCDataIface(sdbusplus::async::context& ctx,
                                   data_sync::Manager& manager) :
    sdbusplus::aserver::xyz::openbmc_project::control::SyncBMCData<
        SyncBMCDataIface>(ctx, SyncBMCData::instance_path),
    _manager(manager), _ctx(ctx)
{
    restoreDBusProperties();
    emit_added();
}

void SyncBMCDataIface::restoreDBusProperties()
{
    try
    {
        auto json =
            data_sync::persist::readFile(data_sync::persist::DBusPropDataFile);
        if (!json)
        {
            return;
        }
        if (auto it = json->find(data_sync::persist::key::disable);
            it != json->end())
        {
            this->properties.disable_sync = it->get<bool>();
        }
        if (auto it = json->find(data_sync::persist::key::fullSyncStatus);
            it != json->end())
        {
            this->properties.full_sync_status = static_cast<FullSyncStatus>(
                it->get<std::underlying_type_t<FullSyncStatus>>());
        }
        if (auto it = json->find(data_sync::persist::key::syncEventsHealth);
            it != json->end())
        {
            this->properties.sync_events_health = static_cast<SyncEventsHealth>(
                it->get<std::underlying_type_t<SyncEventsHealth>>());
        }
        lg2::info(
            "Restored DBus properties - DisableSync: {DISABLE}, FullSyncStatus: {FULLSYNC}, SyncEventsHealth: {HEALTH}",
            "DISABLE", disable_sync(), "FULLSYNC",
            SyncBMCData::convertFullSyncStatusToString(full_sync_status()),
            "HEALTH",
            SyncBMCData::convertSyncEventsHealthToString(sync_events_health()));
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Error trying to restore previous values of DBus properties: {ERROR}",
            "ERROR", e);
    }
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    SyncBMCDataIface::method_call([[maybe_unused]] start_full_sync_t type)
{
    if (disable_sync())
    {
        lg2::error("Sync is Disabled, cannot start full sync.");
        throw sdbusplus::xyz::openbmc_project::Control::SyncBMCData::Error::
            SyncDisabled();
    }

    if (data_sync::Manager::isSiblingBmcNotAvailable())
    {
        lg2::error(
            "Sibling BMC is not available, Unable to retrieve the BMC IP ");
        throw sdbusplus::xyz::openbmc_project::Control::SyncBMCData::Error::
            SiblingBMCNotAvailable();
    }

    if (full_sync_status() == FullSyncStatus::FullSyncInProgress)
    {
        lg2::error(
            "Full Sync in progress. Operation cannot proceed at this time ");
        throw sdbusplus::xyz::openbmc_project::Control::SyncBMCData::Error::
            FullSyncInProgress();
    }

    co_return _ctx.spawn(_manager.startFullSync());
}

bool SyncBMCDataIface::set_property([[maybe_unused]] disable_sync_t type,
                                    bool disable)
{
    if (disable_sync() == disable)
    {
        lg2::info("Sync is already {VALUE}", "VALUE",
                  (disable_sync() ? "disabled" : "enabled"));
        return false;
    }
    this->properties.disable_sync = disable;
    if (sync_events_health() != SyncEventsHealth::Critical)
    {
        _manager.setSyncEventsHealth(disable ? SyncEventsHealth::Paused
                                             : SyncEventsHealth::Ok);
    }
    try
    {
        data_sync::persist::update(data_sync::persist::key::disable, disable);
    }
    catch (const std::exception& e)
    {
        lg2::warning(
            "Failed to serialize DBus Disable Sync value of {DISABLE}: {ERROR}",
            "DISABLE", disable, "ERROR", e);
    }
    _manager.disableSyncPropChanged(disable);
    return true;
}

} // namespace data_sync::dbus_ifaces
