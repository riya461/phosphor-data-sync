// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "data_sync_config.hpp"
#include "external_data_ifaces.hpp"
#include "persistent.hpp"
#include "sync_bmc_data_ifaces.hpp"

#include <filesystem>
#include <ranges>
#include <vector>

namespace data_sync
{

using FullSyncStatus = sdbusplus::common::xyz::openbmc_project::control::
    SyncBMCData::FullSyncStatus;
using SyncEventsHealth = sdbusplus::common::xyz::openbmc_project::control::
    SyncBMCData::SyncEventsHealth;

namespace fs = std::filesystem;

/**
 * @class Manager
 *
 * @brief This class manages all configured data for synchronization
 *        between BMCs.
 */
class Manager
{
  public:
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(Manager&&) = delete;
    ~Manager() = default;

    /**
     * @brief The constructor parses the configuration, monitors the data, and
     *        synchronizes it.
     *
     * @param[in] ctx - The async context
     * @param[in] extDataIfaces - The external data interfaces object
     * @param[in] dataSyncCfgDir - The data sync configuration directory
     */
    Manager(sdbusplus::async::context& ctx,
            std::unique_ptr<ext_data::ExternalDataIFaces>&& extDataIfaces,
            const fs::path& dataSyncCfgDir);

    /**
     * @brief An API helper to verify if the manager contains the given
     *        data sync configuration.
     *
     * @param[in] dataSyncCfg - The data sync configuration to check.
     *
     * @return True if contains; otherwise False.
     */
    bool containsDataSyncCfg(const config::DataSyncConfig& dataSyncCfg)
    {
        return std::ranges::contains(_dataSyncConfiguration, dataSyncCfg);
    }

    /**
     * @brief Initiates a full synchronization between two BMCs.
     *
     *        - This method is responsible for initiating the  Full
     *          synchronization process between two BMCs.
     *        - The sync process is handled asynchronously.
     *
     */
    sdbusplus::async::task<> startFullSync();

    /**
     * @brief Helper API that retrieves the sibling BMC IP and returns its
     *        availability as a boolean.
     *
     * @return True if sibling BMC IP is empty; otherwise False.
     */
    bool isSiblingBmcNotAvailable()
    {
        return _extDataIfaces->siblingBmcIP().empty();
    }

    /**
     * @brief Helper API fetches the full sync Dbus status-property.
     */
    FullSyncStatus getFullSyncStatus() const
    {
        return _syncBMCDataIface.full_sync_status();
    }

    /**
     * @brief Helper API sets the full sync Dbus status-property.
     *
     * @param[in] fullSyncStatus - The Full Sync Status property being set.
     */
    void setFullSyncStatus(const FullSyncStatus& fullSyncStatus);

    /**
     * @brief Helper API to start events when Disable sync property is changed.
     *        - If the Disable sync property is set to true, it stops all sync
     *          events. Otherwise, it starts all sync events.
     *
     * @param[in] disableSync - The Disable sync property value being set.
     */
    void disableSyncPropChanged(bool disableSync);

    /**
     * @brief Helper API to set the Disable sync Dbus status-property.
     *        Specifically, for unit testing purposes.
     *
     * @param[in] disableSync - The Disable sync property value being set.
     */
    void setDisableSyncStatus(bool disableSync)
    {
        _syncBMCDataIface.disable_sync(disableSync);
    }

    /**
     * @brief Helper API fetches the sync events health Dbus property.
     *        Specifically, for unit testing purposes.
     */
    SyncEventsHealth getSyncEventsHealth() const
    {
        return _syncBMCDataIface.sync_events_health();
    }

    /**
     * @brief Helper API sets the sync events health Dbus property.
     *
     * @param[in] syncEventsHealth - The sync events health property value being
     * set.
     */
    void setSyncEventsHealth(const SyncEventsHealth& syncEventsHealth);

  private:
    /**
     * @brief A helper API to start the data sync operation.
     */
    sdbusplus::async::task<> init();

    /**
     * @brief A helper API to parse the data sync configuration
     *
     * @note It will continue parsing all files even if one file fails to parse.
     */
    sdbusplus::async::task<> parseConfiguration();

    /**
     * @brief A helper API to initiate sync events, covering the following
     *        scenarios. These event will be initiated based on the BMC role.
     *
     *        - A file monitor for all configured files that require immediate
     *          synchronization.
     *        - A timer event for all configured files that require periodic
     *          synchronization.
     */
    sdbusplus::async::task<> startSyncEvents();

    /**
     * @brief A helper rsync wrapper API that syncs data to sibling
     *        BMC, with different behavior in the unit test environment,
     *        performing a local copy instead.
     *
     * @param[in] dataSyncCfg - The data sync config to sync
     * @param[in] srcPath - The optional source data path
     * @param[in] destPath - The optional dest path
     * @param[in] retryCount - The current retry attempt number
     *
     * @return Returns true if sync succeeds; otherwise, returns false
     *
     */
    sdbusplus::async::task<bool>
        syncData(const config::DataSyncConfig& dataSyncCfg,
                 fs::path srcPath = fs::path{},
                 const std::string destPath = "",
                 size_t retryCount = 0);

    /**
     * @brief A helper to API to monitor data to sync if its changed
     *
     * @param[in] dataSyncCfg - The data sync config to sync
     *
     */
    sdbusplus::async::task<>
        monitorDataToSync(config::DataSyncConfig& dataSyncCfg);

    /**
     * @brief A helper to API to sync data periodically.
     *
     * @param[in] dataSyncCfg - The data sync config to sync
     */
    sdbusplus::async::task<>
        monitorTimerToSync(config::DataSyncConfig& dataSyncCfg);

    /**
     * @brief A helper to API Checks if the data can be synchronize.
     *
     *        - This API verifies whether the given data meets the criteria
     *          for being synced. It returns a boolean value indicating if
     *          the data is eligible to be synced.
     *
     * @param[in] dataSyncCfg - The data sync config to sync
     *
     * @return True if SyncEligible; otherwise False.
     */
    bool isSyncEligible(const config::DataSyncConfig& dataSyncCfg);

    /**
     * @brief The async context object used to perform operations asynchronously
     *        as required.
     */
    sdbusplus::async::context& _ctx;

    /**
     * @brief An external data interface object used to seamlessly retrieve
     *        external dependent data.
     */
    std::unique_ptr<ext_data::ExternalDataIFaces> _extDataIfaces;

    /**
     * @brief The data sync configuration directory
     */
    std::string _dataSyncCfgDir;
    /**
     * @brief The list of data to synchronize.
     */
    std::vector<config::DataSyncConfig> _dataSyncConfiguration;

    /**
     * @brief SyncBMCData Server Interface object
     */
    dbus_ifaces::SyncBMCDataIface _syncBMCDataIface;
};

} // namespace data_sync
