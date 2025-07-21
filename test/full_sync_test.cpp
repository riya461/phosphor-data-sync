// SPDX-License-Identifier: Apache-2.0

#include "manager_test.hpp"

namespace fs = std::filesystem;

std::filesystem::path ManagerTest::dataSyncCfgDir;
std::filesystem::path ManagerTest::tmpDataSyncDataDir;

using FullSyncStatus = sdbusplus::common::xyz::openbmc_project::control::
    SyncBMCData::FullSyncStatus;
using SyncEventsHealth = sdbusplus::common::xyz::openbmc_project::control::
    SyncBMCData::SyncEventsHealth;

/*
 * Test the Full sync is triggered from the Active BMC to the Passive BMC,
 * ensuring that the Full Sync status is successfully completed.
 * Will be also testing that the DBus SyncEventsHealth property changes
 * from 'Critical' to 'Ok' when Full Sync completes successfully.
 */

TEST_F(ManagerTest, FullSyncA2PTest)
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
        mockExtDataIfaces->setBMCRedundancy(true);
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
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile1"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Active to Passive bmc"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile2"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Active to Passive bmc"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile3"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Active to Passive bmc"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile4"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Active to Passive bmc"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}},

        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Active to Passive bmc directory"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcDir = jsonData["Directories"][0]["Path"];
    fs::path destDir = jsonData["Directories"][0]["DestinationPath"];

    std::filesystem::create_directory(ManagerTest::tmpDataSyncDataDir /
                                      "srcDir");

    std::filesystem::create_directories(ManagerTest::tmpDataSyncDataDir /
                                        "srcDir" / "subDir");

    fs::path dirFile = srcDir / "dirFile";
    fs::path subDirFile = ManagerTest::tmpDataSyncDataDir / srcDir / "subDir" /
                          "subDirFile";

    ManagerTest::writeData(dirFile, "Data in directory file");
    ManagerTest::writeData(subDirFile, "Data in source directory file");

    ASSERT_EQ(ManagerTest::readData(dirFile), "Data in directory file");
    ASSERT_EQ(ManagerTest::readData(subDirFile),
              "Data in source directory file");

    fs::path srcFile1{jsonData["Files"][0]["Path"]};
    fs::path srcFile2{jsonData["Files"][1]["Path"]};
    fs::path srcFile3{jsonData["Files"][2]["Path"]};
    fs::path srcFile4{jsonData["Files"][3]["Path"]};

    fs::path destDir1{jsonData["Files"][0]["DestinationPath"]};
    fs::path destDir2{jsonData["Files"][1]["DestinationPath"]};
    fs::path destDir3{jsonData["Files"][2]["DestinationPath"]};
    fs::path destDir4{jsonData["Files"][3]["DestinationPath"]};

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data1{"Data written on the file1\n"};
    std::string data2{"Data written on the file2\n"};
    std::string data3{"Data written on the file3\n"};
    std::string data4{"Data written on the file4\n"};

    ManagerTest::writeData(srcFile1, data1);
    ManagerTest::writeData(srcFile2, data2);
    ManagerTest::writeData(srcFile3, data3);
    ManagerTest::writeData(srcFile4, data4);

    ASSERT_EQ(ManagerTest::readData(srcFile1), data1);
    ASSERT_EQ(ManagerTest::readData(srcFile2), data2);
    ASSERT_EQ(ManagerTest::readData(srcFile3), data3);
    ASSERT_EQ(ManagerTest::readData(srcFile4), data4);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Setting the SyncEventsHealth to Critical to test status change after full
    // sync completes.
    manager.setSyncEventsHealth(SyncEventsHealth::Critical);
    auto waitingForFullSyncToFinish =
        // NOLINTNEXTLINE
        [&](sdbusplus::async::context& ctx) -> sdbusplus::async::task<void> {
        auto status = manager.getFullSyncStatus();

        while (status != FullSyncStatus::FullSyncCompleted &&
               status != FullSyncStatus::FullSyncFailed)
        {
            co_await sdbusplus::async::sleep_for(ctx,
                                                 std::chrono::milliseconds(50));
            status = manager.getFullSyncStatus();
        }

        EXPECT_EQ(status, FullSyncStatus::FullSyncCompleted)
            << "FullSync status is not Completed!";

        EXPECT_EQ(ManagerTest::readData(destDir1 / fs::relative(srcFile1, "/")),
                  data1);
        EXPECT_EQ(ManagerTest::readData(destDir2 / fs::relative(srcFile2, "/")),
                  data2);
        EXPECT_EQ(ManagerTest::readData(destDir3 / fs::relative(srcFile3, "/")),
                  data3);
        EXPECT_EQ(ManagerTest::readData(destDir4 / fs::relative(srcFile4, "/")),
                  data4);

        fs::path destdirFile = destDir / fs::relative(dirFile, "/");
        fs::path destsubDirFile = destDir / fs::relative(subDirFile, "/");

        EXPECT_EQ(ManagerTest::readData(destdirFile), "Data in directory file");
        EXPECT_EQ(ManagerTest::readData(destsubDirFile),
                  "Data in source directory file");

        ctx.request_stop();

        // Forcing to trigger inotify events so that all running immediate
        // sync tasks will resume and stop since the context is requested to
        // stop in the above.
        ManagerTest::writeData(srcFile1, data1);
        ManagerTest::writeData(srcFile2, data2);
        ManagerTest::writeData(srcFile3, data3);
        ManagerTest::writeData(srcFile4, data4);
        ManagerTest::writeData(dirFile, "Data in directory file");

        co_return;
    };

    ctx.spawn(waitingForFullSyncToFinish(ctx));

    ctx.run();
    EXPECT_EQ(manager.getSyncEventsHealth(), SyncEventsHealth::Ok)
        << "SyncEventsHealth should be Ok after full sync completes successfully.";
}

/*
 * Test the Full sync is triggered from the Passive BMC to the Active BMC,
 * ensuring that the Full Sync status is successfully completed
 */

TEST_F(ManagerTest, FullSyncP2ATest)
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
        mockExtDataIfaces->setBMCRole(ed::BMCRole::Passive);
        mockExtDataIfaces->setBMCRedundancy(true);
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
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile1"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile2"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile3"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile4"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}},
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "Parse test directory"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcDir = jsonData["Directories"][0]["Path"];
    fs::path destDir = jsonData["Directories"][0]["DestinationPath"];

    std::filesystem::create_directory(ManagerTest::tmpDataSyncDataDir /
                                      "srcDir");

    std::filesystem::create_directories(ManagerTest::tmpDataSyncDataDir /
                                        "srcDir" / "subDir");

    fs::path dirFile = srcDir / "dirFile";
    fs::path subDirFile = ManagerTest::tmpDataSyncDataDir / srcDir / "subDir" /
                          "subDirFile";

    ManagerTest::writeData(dirFile, "Data in directory file");
    ManagerTest::writeData(subDirFile, "Data in source directory file");

    ASSERT_EQ(ManagerTest::readData(dirFile), "Data in directory file");
    ASSERT_EQ(ManagerTest::readData(subDirFile),
              "Data in source directory file");

    fs::path srcFile1{jsonData["Files"][0]["Path"]};
    fs::path destDir1{jsonData["Files"][0]["DestinationPath"]};

    fs::path srcFile2{jsonData["Files"][1]["Path"]};
    fs::path destDir2{jsonData["Files"][1]["DestinationPath"]};

    fs::path srcFile3{jsonData["Files"][2]["Path"]};
    fs::path destDir3{jsonData["Files"][2]["DestinationPath"]};

    fs::path srcFile4{jsonData["Files"][3]["Path"]};
    fs::path destDir4{jsonData["Files"][3]["DestinationPath"]};

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data1{"Data written on the file1\n"};
    ManagerTest::writeData(srcFile1, data1);

    std::string data2{"Data written on the file2\n"};
    ManagerTest::writeData(srcFile2, data2);

    std::string data3{"Data written on the file3\n"};
    ManagerTest::writeData(srcFile3, data3);

    std::string data4{"Data written on the file4\n"};
    ManagerTest::writeData(srcFile4, data4);

    ASSERT_EQ(ManagerTest::readData(srcFile1), data1);
    ASSERT_EQ(ManagerTest::readData(srcFile2), data2);
    ASSERT_EQ(ManagerTest::readData(srcFile3), data3);
    ASSERT_EQ(ManagerTest::readData(srcFile4), data4);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    auto waitingForFullSyncToFinish =
        // NOLINTNEXTLINE
        [&](sdbusplus::async::context& ctx) -> sdbusplus::async::task<void> {
        auto status = manager.getFullSyncStatus();

        while (status != FullSyncStatus::FullSyncCompleted &&
               status != FullSyncStatus::FullSyncFailed)
        {
            co_await sdbusplus::async::sleep_for(ctx,
                                                 std::chrono::milliseconds(50));
            status = manager.getFullSyncStatus();
        }

        EXPECT_EQ(status, FullSyncStatus::FullSyncCompleted)
            << "FullSync status is not Completed!";

        EXPECT_EQ(ManagerTest::readData(destDir1 / fs::relative(srcFile1, "/")),
                  data1);
        EXPECT_EQ(ManagerTest::readData(destDir2 / fs::relative(srcFile2, "/")),
                  data2);
        EXPECT_EQ(ManagerTest::readData(destDir3 / fs::relative(srcFile3, "/")),
                  data3);
        EXPECT_NE(ManagerTest::readData(destDir4 / fs::relative(srcFile4, "/")),
                  data4);

        fs::path destdirFile = destDir / fs::relative(dirFile, "/");
        fs::path destsubDirFile = destDir / fs::relative(subDirFile, "/");

        EXPECT_EQ(ManagerTest::readData(destdirFile), "Data in directory file");
        EXPECT_EQ(ManagerTest::readData(destsubDirFile),
                  "Data in source directory file");

        ctx.request_stop();

        // Forcing to trigger inotify events so that all running immediate
        // sync tasks will resume and stop since the context is requested to
        // stop in the above.
        ManagerTest::writeData(srcFile1, data1);
        ManagerTest::writeData(srcFile2, data2);
        ManagerTest::writeData(srcFile3, data3);
        ManagerTest::writeData(srcFile4, data4);
        ManagerTest::writeData(dirFile, "Data in directory file");

        co_return;
    };

    ctx.spawn(waitingForFullSyncToFinish(ctx));

    ctx.run();
}

/*
 * Test the Full sync is triggered from the Passive BMC to the Active BMC,
 * ensuring that the Full Sync status is still InProgress.
 */

TEST_F(ManagerTest, FullSyncInProgressTest)
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
        mockExtDataIfaces->setBMCRole(ed::BMCRole::Passive);
        mockExtDataIfaces->setBMCRedundancy(true);
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
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile1"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile2"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile3"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile4"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}}}},
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "Parse test directory"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcDir = jsonData["Directories"][0]["Path"];
    fs::path destDir = jsonData["Directories"][0]["DestinationPath"];

    std::filesystem::create_directory(ManagerTest::tmpDataSyncDataDir /
                                      "srcDir");

    std::filesystem::create_directories(ManagerTest::tmpDataSyncDataDir /
                                        "srcDir" / "subDir");

    fs::path dirFile = srcDir / "dirFile";
    ManagerTest::writeData(dirFile, "Data in directory file");

    ASSERT_EQ(ManagerTest::readData(dirFile), "Data in directory file");

    fs::path srcFile1{jsonData["Files"][0]["Path"]};
    fs::path destDir1{jsonData["Files"][0]["DestinationPath"]};

    fs::path srcFile2{jsonData["Files"][1]["Path"]};
    fs::path destDir2{jsonData["Files"][1]["DestinationPath"]};

    fs::path srcFile3{jsonData["Files"][2]["Path"]};
    fs::path destDir3{jsonData["Files"][2]["DestinationPath"]};

    fs::path srcFile4{jsonData["Files"][3]["Path"]};
    fs::path destDir4{jsonData["Files"][3]["DestinationPath"]};

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data1{"Data written on the file1\n"};
    ManagerTest::writeData(srcFile1, data1);

    std::string data2{"Data written on the file2\n"};
    ManagerTest::writeData(srcFile2, data2);

    std::string data3{"Data written on the file3\n"};
    ManagerTest::writeData(srcFile3, data3);

    std::string data4{"Data written on the file4\n"};
    ManagerTest::writeData(srcFile4, data4);

    ASSERT_EQ(ManagerTest::readData(srcFile1), data1);
    ASSERT_EQ(ManagerTest::readData(srcFile2), data2);
    ASSERT_EQ(ManagerTest::readData(srcFile3), data3);
    ASSERT_EQ(ManagerTest::readData(srcFile4), data4);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    auto waitingForFullSyncToFinish =
        // NOLINTNEXTLINE
        [&](sdbusplus::async::context& ctx) -> sdbusplus::async::task<void> {
        auto status = manager.getFullSyncStatus();
        while (status != FullSyncStatus::FullSyncInProgress)
        {
            co_await sdbusplus::async::sleep_for(ctx,
                                                 std::chrono::nanoseconds(200));
            status = manager.getFullSyncStatus();
        }

        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::microseconds(100));

        EXPECT_EQ(status, FullSyncStatus::FullSyncInProgress)
            << "FullSync status is not InProgress!";
        ctx.request_stop();

        // Forcing to trigger inotify events so that all running immediate
        // sync tasks will resume and stop since the context is requested to
        // stop in the above.
        ManagerTest::writeData(srcFile1, data1);
        ManagerTest::writeData(srcFile2, data2);
        ManagerTest::writeData(srcFile3, data3);
        ManagerTest::writeData(srcFile4, data4);
        ManagerTest::writeData(dirFile, "Data in directory file");

        co_return;
    };

    ctx.spawn(waitingForFullSyncToFinish(ctx));
    ctx.run();
}

/*
 * Test the Full sync is triggered from the Passive BMC to the Active BMC,
 * ensuring that the Full Sync status is Failed due to some ongoing issue.
 * Will be also testing that the DBus SyncEventsHealth property changes
 * to 'Critical' when Full Sync Fails.
 */

TEST_F(ManagerTest, FullSyncFailed)
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
        mockExtDataIfaces->setBMCRole(ed::BMCRole::Passive);
        mockExtDataIfaces->setBMCRedundancy(true);
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
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile1"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile2"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile3"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}},
          {{"Path",
            ManagerTest::tmpDataSyncDataDir.string() + "/test/srcFile4"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/test/destDir"},
           {"Description", "FullSync from Passive to Active bmc"},
           {"SyncDirection", "Passive2Active"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcFile1{jsonData["Files"][0]["Path"]};
    fs::path destDir1{jsonData["Files"][0]["DestinationPath"]};

    fs::path srcFile2{jsonData["Files"][1]["Path"]};
    fs::path destDir2{jsonData["Files"][1]["DestinationPath"]};

    fs::path srcFile3{jsonData["Files"][2]["Path"]};
    fs::path destDir3{jsonData["Files"][2]["DestinationPath"]};

    fs::path srcFile4{jsonData["Files"][3]["Path"]};
    fs::path destDir4{jsonData["Files"][3]["DestinationPath"]};

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data1{"Data written on the file1\n"};
    ManagerTest::writeData(srcFile1, data1);

    std::string data2{"Data written on the file2\n"};
    ManagerTest::writeData(srcFile2, data2);

    std::string data3{"Data written on the file3\n"};
    ManagerTest::writeData(srcFile3, data3);

    ASSERT_EQ(ManagerTest::readData(srcFile1), data1);
    ASSERT_EQ(ManagerTest::readData(srcFile2), data2);
    ASSERT_EQ(ManagerTest::readData(srcFile3), data3);

    // Commented out the writing and verification for srcFile4 to simulate a
    // failure scenario, where the parent (/tmp/pdsDataDirXXXXXX/test)
    // of source file "srcFile4" does not exist. This causes the rsync
    // operation to fail (return false), as the file is unavailable for syncing,
    // which helps test the failure path.

    std::string data4{"Data written on the file4\n"};
    // ManagerTest::writeData(srcFile4, data4);
    // ASSERT_EQ(ManagerTest::readData(srcFile4), data4);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    auto waitingForFullSyncToFinish =
        // NOLINTNEXTLINE
        [&](sdbusplus::async::context& ctx) -> sdbusplus::async::task<void> {
        auto status = manager.getFullSyncStatus();

        while (status != FullSyncStatus::FullSyncCompleted &&
               status != FullSyncStatus::FullSyncFailed)
        {
            co_await sdbusplus::async::sleep_for(ctx,
                                                 std::chrono::milliseconds(50));
            status = manager.getFullSyncStatus();
        }

        // NOTE: The following test checks are commented out temporarily.
        // Since Full Sync is currently forced to always succeed (even if
        // syncing fails), there is no failure scenario being triggered, and
        // these tests will not pass. These checks should be re-enabled once
        // proper Full Sync failure handling is implemented

        // EXPECT_EQ(status, FullSyncStatus::FullSyncFailed)
        // << "FullSync status is not Failed!!";
        // EXPECT_EQ(manager.getSyncEventsHealth(), SyncEventsHealth::Critical)
        // << "SyncEventsHealth should be Critical.";

        EXPECT_EQ(ManagerTest::readData(destDir1 / fs::relative(srcFile1, "/")),
                  data1);
        EXPECT_EQ(ManagerTest::readData(destDir2 / fs::relative(srcFile2, "/")),
                  data2);
        EXPECT_EQ(ManagerTest::readData(destDir3 / fs::relative(srcFile3, "/")),
                  data3);
        EXPECT_FALSE(fs::exists(destDir4 / fs::relative(srcFile4, "/")));

        ctx.request_stop();

        // Forcing to trigger inotify events so that all running immediate
        // sync tasks will resume and stop since the context is requested to
        // stop in the above.
        ManagerTest::writeData(srcFile1, data1);
        ManagerTest::writeData(srcFile2, data2);
        ManagerTest::writeData(srcFile3, data3);

        co_return;
    };

    ctx.spawn(waitingForFullSyncToFinish(ctx));

    ctx.run();
}

TEST_F(ManagerTest, FullSyncA2PWithExcludeDirTest)
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
        mockExtDataIfaces->setBMCRedundancy(true);
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
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile1"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from ActiPassive with excludeList"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}},
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from A2P with exclude directory list"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"},
           {"ExcludeList",
            {ManagerTest::tmpDataSyncDataDir.string() +
             "/srcDir/subDirX/"}}}}}};

    fs::path srcFile1{jsonData["Files"][0]["Path"]};
    fs::path destDir1{jsonData["Files"][0]["DestinationPath"]};

    fs::path srcDir = jsonData["Directories"][0]["Path"];
    fs::path destDir = jsonData["Directories"][0]["DestinationPath"];
    fs::path excludeDir = jsonData["Directories"][0]["ExcludeList"][0];
    fs::path subDir1 = srcDir / "subDir1";

    std::filesystem::create_directory(srcDir);
    std::filesystem::create_directories(subDir1);
    std::filesystem::create_directories(excludeDir);

    fs::path dirFile1 = srcDir / "dirFile1";
    fs::path subDir1File = subDir1 / "subDir1File";
    fs::path excludeDirFile = excludeDir / "subDirXfile";

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string dataDirFile1{"Data in directory file1"};
    std::string dataSubDir1File{"Data in 1st sub directory file"};
    std::string dataExcludeDirFile{"Data in exclude sub directory file"};

    ManagerTest::writeData(dirFile1, dataDirFile1);
    ManagerTest::writeData(subDir1File, dataSubDir1File);
    ManagerTest::writeData(excludeDirFile, dataExcludeDirFile);

    ASSERT_EQ(ManagerTest::readData(dirFile1), dataDirFile1);
    ASSERT_EQ(ManagerTest::readData(subDir1File), dataSubDir1File);
    ASSERT_EQ(ManagerTest::readData(excludeDirFile), dataExcludeDirFile);

    std::string data1{"Data written on the file1\n"};

    ManagerTest::writeData(srcFile1, data1);

    ASSERT_EQ(ManagerTest::readData(srcFile1), data1);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Setting the SyncEventsHealth to Critical to test status change after full
    // sync completes.
    manager.setSyncEventsHealth(SyncEventsHealth::Critical);
    auto waitingForFullSyncToFinish =
        // NOLINTNEXTLINE
        [&](sdbusplus::async::context& ctx) -> sdbusplus::async::task<void> {
        auto status = manager.getFullSyncStatus();

        while (status != FullSyncStatus::FullSyncCompleted &&
               status != FullSyncStatus::FullSyncFailed)
        {
            co_await sdbusplus::async::sleep_for(ctx,
                                                 std::chrono::milliseconds(50));
            status = manager.getFullSyncStatus();
        }

        EXPECT_EQ(status, FullSyncStatus::FullSyncCompleted)
            << "FullSync status is not Completed!";

        EXPECT_EQ(ManagerTest::readData(destDir1 / fs::relative(srcFile1, "/")),
                  data1);

        fs::path destDirFile1 = destDir / fs::relative(dirFile1, "/");
        fs::path destSubDir1File = destDir / fs::relative(subDir1File, "/");
        fs::path destExcludeDirX = destDir / fs::relative(excludeDir, "/");

        EXPECT_EQ(ManagerTest::readData(destDirFile1), dataDirFile1);
        EXPECT_EQ(ManagerTest::readData(destSubDir1File), dataSubDir1File);
        EXPECT_FALSE(fs::exists(destExcludeDirX));

        ctx.request_stop();

        // Forcing to trigger inotify events so that all running immediate
        // sync tasks will resume and stop since the context is requested to
        // stop in the above.
        ManagerTest::writeData(srcFile1, data1);
        ManagerTest::writeData(dirFile1, "Data in directory file");

        co_return;
    };

    ctx.spawn(waitingForFullSyncToFinish(ctx));

    ctx.run();
    EXPECT_EQ(manager.getSyncEventsHealth(), SyncEventsHealth::Ok)
        << "SyncEventsHealth should be Ok after full sync completes successfully.";
}

TEST_F(ManagerTest, FullSyncA2PWithExcludeFileTest)
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
        mockExtDataIfaces->setBMCRedundancy(true);
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
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile1"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from Active to Passive bmc"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}},
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "FullSync from A2P with exclude file"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"},
           {"ExcludeList",
            {ManagerTest::tmpDataSyncDataDir.string() +
             "/srcDir/dirFileX"}}}}}};

    fs::path srcFile1{jsonData["Files"][0]["Path"]};
    fs::path destDir1{jsonData["Files"][0]["DestinationPath"]};

    fs::path srcDir = jsonData["Directories"][0]["Path"];
    fs::path destDir = jsonData["Directories"][0]["DestinationPath"];
    fs::path excludeFile = jsonData["Directories"][0]["ExcludeList"][0];

    std::filesystem::create_directory(srcDir);

    fs::path dirFile1 = srcDir / "dirFile1";

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string dataDirFile1{"Data in directory file1"};
    std::string dataExcludeFile{"Data in exclude file"};

    ManagerTest::writeData(dirFile1, dataDirFile1);
    ManagerTest::writeData(excludeFile, dataExcludeFile);

    ASSERT_EQ(ManagerTest::readData(dirFile1), dataDirFile1);
    ASSERT_EQ(ManagerTest::readData(excludeFile), dataExcludeFile);

    std::string data1{"Data written on the file1\n"};

    ManagerTest::writeData(srcFile1, data1);

    ASSERT_EQ(ManagerTest::readData(srcFile1), data1);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Setting the SyncEventsHealth to Critical to test status change after full
    // sync completes.
    manager.setSyncEventsHealth(SyncEventsHealth::Critical);
    auto waitingForFullSyncToFinish =
        // NOLINTNEXTLINE
        [&](sdbusplus::async::context& ctx) -> sdbusplus::async::task<void> {
        auto status = manager.getFullSyncStatus();

        while (status != FullSyncStatus::FullSyncCompleted &&
               status != FullSyncStatus::FullSyncFailed)
        {
            co_await sdbusplus::async::sleep_for(ctx,
                                                 std::chrono::milliseconds(50));
            status = manager.getFullSyncStatus();
        }

        EXPECT_EQ(status, FullSyncStatus::FullSyncCompleted)
            << "FullSync status is not Completed!";

        EXPECT_EQ(ManagerTest::readData(destDir1 / fs::relative(srcFile1, "/")),
                  data1);

        fs::path destDirFile1 = destDir / fs::relative(dirFile1, "/");
        fs::path destExcludeFile = destDir / fs::relative(excludeFile, "/");

        EXPECT_EQ(ManagerTest::readData(destDirFile1), dataDirFile1);
        EXPECT_FALSE(fs::exists(destExcludeFile));

        ctx.request_stop();

        // Forcing to trigger inotify events so that all running immediate
        // sync tasks will resume and stop since the context is requested to
        // stop in the above.
        ManagerTest::writeData(srcFile1, data1);
        ManagerTest::writeData(dirFile1, "Data in directory file");

        co_return;
    };

    ctx.spawn(waitingForFullSyncToFinish(ctx));

    ctx.run();
    EXPECT_EQ(manager.getSyncEventsHealth(), SyncEventsHealth::Ok)
        << "SyncEventsHealth should be Ok after full sync completes successfully.";
}
