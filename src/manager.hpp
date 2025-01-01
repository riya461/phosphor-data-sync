// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "data_sync_config.hpp"

#include <sdbusplus/async.hpp>

#include <filesystem>
#include <ranges>
#include <vector>

namespace data_sync
{

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
     * @param[in] dataSyncCfgDir - The data sync configuration directory
     */
    Manager(sdbusplus::async::context& ctx, const fs::path& dataSyncCfgDir);

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
     * @brief The async context object used to perform operations asynchronously
     *        as required.
     */
    sdbusplus::async::context& _ctx;

    /**
     * @brief The data sync configuration directory
     */
    std::string _dataSyncCfgDir;
    /**
     * @brief The list of data to synchronize.
     */
    std::vector<config::DataSyncConfig> _dataSyncConfiguration;
};

} // namespace data_sync
