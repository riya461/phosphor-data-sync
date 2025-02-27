// SPDX-License-Identifier: Apache-2.0
#include "manager_test.hpp"

std::filesystem::path ManagerTest::dataSyncCfgDir;
std::filesystem::path ManagerTest::tmpDataSyncDataDir;
nlohmann::json ManagerTest::commonJsonData;

TEST_F(ManagerTest, ParseDataSyncCfg)
{
    using namespace std::literals;
    namespace ed = data_sync::ext_data;

    commonJsonData = R"(
            {
                "Files": [
                    {
                        "Path": "/file/path/to/sync",
                        "Description": "Parse test file",
                        "SyncDirection": "Active2Passive",
                        "SyncType": "Immediate"
                    }
                ],
                "Directories": [
                    {
                        "Path": "/directory/path/to/sync",
                        "Description": "Parse test directory",
                        "SyncDirection": "Passive2Active",
                        "SyncType": "Periodic",
                        "Periodicity": "PT1S",
                        "RetryAttempts": 1,
                        "RetryInterval": "PT10M",
                        "ExcludeFilesList": ["/directory/file/to/ignore"],
                        "IncludeFilesList": ["/directory/file/to/consider"]
                    }
                ]
            }
        )"_json;

    std::filesystem::path dataSyncCommonCfgFile{dataSyncCfgDir /
                                                "common_test_config.json"};
    std::ofstream cfgFile(dataSyncCommonCfgFile);
    ASSERT_TRUE(cfgFile.is_open())
        << "Failed to open " << dataSyncCommonCfgFile;
    cfgFile << commonJsonData;
    cfgFile.close();

    std::unique_ptr<ed::ExternalDataIFaces> extDataIface =
        std::make_unique<ed::MockExternalDataIFaces>();

    ed::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<ed::MockExternalDataIFaces*>(extDataIface.get());

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    sdbusplus::async::context ctx;

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    EXPECT_FALSE(manager.containsDataSyncCfg(data_sync::config::DataSyncConfig(
        ManagerTest::commonJsonData["Files"][0], false)));

    ctx.spawn(
        sdbusplus::async::sleep_for(ctx, 1ns) |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    EXPECT_TRUE(manager.containsDataSyncCfg(data_sync::config::DataSyncConfig(
        ManagerTest::commonJsonData["Files"][0], false)));
}

TEST_F(ManagerTest, PeriodicDisablePropertyTest)
{
    using namespace std::literals;
    namespace ed = data_sync::ext_data;

    std::unique_ptr<ed::ExternalDataIFaces> extDataIface =
        std::make_unique<ed::MockExternalDataIFaces>();

    ed::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<ed::MockExternalDataIFaces*>(extDataIface.get());

    ON_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillByDefault([&mockExtDataIfaces]() -> sdbusplus::async::task<> {
        mockExtDataIfaces->setBMCRole(ed::BMCRole::Active);
        co_return;
    });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Files",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile2"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destFile2"},
           {"Description", "Parse test file"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Periodic"},
           {"Periodicity", "PT1S"}}}}};

    std::string srcFile{jsonData["Files"][0]["Path"]};
    std::string destFile{jsonData["Files"][0]["DestinationPath"]};

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Initial Data\n"};
    ManagerTest::writeData(srcFile, data);

    ASSERT_EQ(ManagerTest::readData(srcFile), data);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};
    manager.setDisableSyncStatus(true); // disabled the sync events

    EXPECT_NE(ManagerTest::readData(destFile), data)
        << "The data should not match because the manager is spawned and"
        << " is waiting for the periodic interval to initiate the sync.";

    ctx.spawn(sdbusplus::async::sleep_for(ctx, 1.1s) |
              sdbusplus::async::execution::then([&destFile, &data, &manager]() {
        EXPECT_NE(ManagerTest::readData(destFile), data)
            << "The data should not match as sync is disabled even though "
            << "sync should take place every 1s as per config";
        manager.setDisableSyncStatus(false); // Trigger the sync events
    }));

    ctx.spawn(
        sdbusplus::async::sleep_for(ctx, 2.2s) |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();
    EXPECT_EQ(ManagerTest::readData(destFile), data)
        << "The data should match with the data as 2.2s is passed"
        << " and sync should take place every 1s as per config.";
}
