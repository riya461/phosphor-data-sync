// SPDX-License-Identifier: Apache-2.0
#include "manager_test.hpp"

std::filesystem::path ManagerTest::dataSyncCfgDir;
std::filesystem::path ManagerTest::tmpDataSyncDataDir;
std::filesystem::path ManagerTest::destDir;

using FullSyncStatus = sdbusplus::common::xyz::openbmc_project::control::
    SyncBMCData::FullSyncStatus;
using SyncEventsHealth = sdbusplus::common::xyz::openbmc_project::control::
    SyncBMCData::SyncEventsHealth;

TEST_F(ManagerTest, testReadWritePersistencyFile)
{
    // Write
    data_sync::persist::update("Disable", true);
    data_sync::persist::update("FullSyncStatus",
                               FullSyncStatus::FullSyncInProgress);
    data_sync::persist::update("SyncEventsHealth", SyncEventsHealth::Critical);

    // Read back
    EXPECT_EQ(data_sync::persist::read<bool>("Disable"), true);
    EXPECT_EQ(data_sync::persist::read<FullSyncStatus>("FullSyncStatus"),
              FullSyncStatus::FullSyncInProgress);
    EXPECT_EQ(data_sync::persist::read<SyncEventsHealth>("SyncEventsHealth"),
              SyncEventsHealth::Critical);

    // Write new values
    data_sync::persist::update("Disable", false);
    data_sync::persist::update("FullSyncStatus",
                               FullSyncStatus::FullSyncCompleted);
    data_sync::persist::update("SyncEventsHealth", SyncEventsHealth::Ok);

    // Read back the new values
    EXPECT_EQ(data_sync::persist::read<bool>("Disable"), false);
    EXPECT_EQ(data_sync::persist::read<FullSyncStatus>("FullSyncStatus"),
              FullSyncStatus::FullSyncCompleted);
    EXPECT_EQ(data_sync::persist::read<SyncEventsHealth>("SyncEventsHealth"),
              SyncEventsHealth::Ok);

    // Some different types - write
    data_sync::persist::update("EmptyString", std::string{});
    data_sync::persist::update("VectorOfStrings",
                               std::vector<std::string>{"a", "b"});
    data_sync::persist::update("EmptyVector", std::vector<std::string>{});

    // Some different types - read back
    EXPECT_EQ(data_sync::persist::read<std::string>("EmptyString"),
              std::string{});
    EXPECT_EQ(
        data_sync::persist::read<std::vector<std::string>>("VectorOfStrings"),
        (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(data_sync::persist::read<std::vector<std::string>>("EmptyVector"),
              (std::vector<std::string>{}));

    // Key doesn't exist
    EXPECT_EQ(data_sync::persist::read<bool>("Blah"), std::nullopt);

    // File doesn't exist
    EXPECT_EQ(data_sync::persist::read<bool>("Disable", "/blah/blah"),
              std::nullopt);

    // Invalid JSON
    std::filesystem::remove(data_sync::persist::DBusPropDataFile);
    std::ofstream file{data_sync::persist::DBusPropDataFile};
    const char* data = R"(
        {
            "FullSyncStatus": 1,
            Bool 0
        }
    )";
    file << data;
    file.close();

    EXPECT_EQ(data_sync::persist::read<FullSyncStatus>(
                  "FullSyncStatus", data_sync::persist::DBusPropDataFile),
              std::nullopt);
}
