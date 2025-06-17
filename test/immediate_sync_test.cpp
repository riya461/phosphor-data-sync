#include "data_watcher.hpp"
#include "manager_test.hpp"

#include <sdbusplus/async.hpp>

#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

std::filesystem::path ManagerTest::dataSyncCfgDir;
std::filesystem::path ManagerTest::tmpDataSyncDataDir;
nlohmann::json ManagerTest::commonJsonData;

TEST_F(ManagerTest, testDataChangeInFile)
{
    using namespace std::literals;
    namespace extData = data_sync::ext_data;

    std::unique_ptr<extData::ExternalDataIFaces> extDataIface =
        std::make_unique<extData::MockExternalDataIFaces>();

    extData::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<extData::MockExternalDataIFaces*>(extDataIface.get());

    ON_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillByDefault([&mockExtDataIfaces]() -> sdbusplus::async::task<> {
        mockExtDataIfaces->setBMCRole(extData::BMCRole::Active);
        co_return;
    });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcPos())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Files",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destFile"},
           {"Description", "File to test immediate sync upon data write"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    std::string srcPath{jsonData["Files"][0]["Path"]};
    std::string destPath{jsonData["Files"][0]["DestinationPath"]};

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Src: Initial Data\n"};
    // Just for create path in the dest to watch for expectation check
    std::string destData{"Dest: Initial Data\n"};
    ManagerTest::writeData(srcPath, data);
    ManagerTest::writeData(destPath, destData);
    ASSERT_EQ(ManagerTest::readData(srcPath), data);
    ASSERT_EQ(ManagerTest::readData(destPath), destData);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    std::string dataToWrite{"Data is modified"};

    // Watch for dest path data change
    data_sync::watch::inotify::DataWatcher dataWatcher(
        ctx, IN_NONBLOCK, IN_CLOSE_WRITE, destPath,
        std::optional<std::vector<std::filesystem::path>>{},
        std::optional<std::vector<std::filesystem::path>>{});
    ctx.spawn(
        dataWatcher.onDataChange() |
        sdbusplus::async::execution::then(
            [&dataToWrite, &destPath]([[maybe_unused]] const auto& dataOps) {
        EXPECT_EQ(dataToWrite, readData(destPath));
    }));

    // Write data after 1s so that the background sync events will be ready
    // to catch.
    ctx.spawn(
        sdbusplus::async::sleep_for(ctx, 1s) |
        sdbusplus::async::execution::then([&ctx, &srcPath, &dataToWrite]() {
        ManagerTest::writeData(srcPath, dataToWrite);
        ASSERT_EQ(ManagerTest::readData(srcPath), dataToWrite);
        ctx.request_stop();
    }));

    ctx.run();
}

TEST_F(ManagerTest, testDataDeleteInDir)
{
    using namespace std::literals;
    namespace extData = data_sync::ext_data;

    std::unique_ptr<extData::ExternalDataIFaces> extDataIface =
        std::make_unique<extData::MockExternalDataIFaces>();

    extData::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<extData::MockExternalDataIFaces*>(extDataIface.get());

    ON_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillByDefault([&mockExtDataIfaces]() -> sdbusplus::async::task<> {
        mockExtDataIfaces->setBMCRole(extData::BMCRole::Active);
        co_return;
    });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcPos())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "Directory to test immediate sync on file deletion"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    std::string srcDir{jsonData["Directories"][0]["Path"]};
    std::string destDir{jsonData["Directories"][0]["DestinationPath"]};

    // Create directories in source and destination
    std::filesystem::create_directory(srcDir);
    std::filesystem::create_directory(destDir);

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Src: Initial Data\n"};
    // Just for create path in the dest to watch for expectation check
    std::string destData{"Dest: Initial Data\n"};

    std::string srcDirFile = srcDir + "Test";
    std::string destDirFile = destDir + "Test";

    ManagerTest::writeData(srcDirFile, data);
    ManagerTest::writeData(destDirFile, destData);
    ASSERT_EQ(ManagerTest::readData(srcDirFile), data);
    ASSERT_EQ(ManagerTest::readData(destDirFile), destData);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Watch for dest path data change
    data_sync::watch::inotify::DataWatcher dataWatcher(
        ctx, IN_NONBLOCK, IN_DELETE, destDir,
        std::optional<std::vector<std::filesystem::path>>{},
        std::optional<std::vector<std::filesystem::path>>{});
    ctx.spawn(dataWatcher.onDataChange() |
              sdbusplus::async::execution::then(
                  [&destDirFile]([[maybe_unused]] const auto& dataOps) {
        // the file should not exists

        EXPECT_FALSE(std::filesystem::exists(destDirFile));
    }));

    // Remove file after 1s so that the background sync events will be ready
    // to catch.
    ctx.spawn(sdbusplus::async::sleep_for(ctx, 1s) |
              sdbusplus::async::execution::then([&ctx, &srcDirFile]() {
        // remove the file from srcDir
        std::filesystem::remove(srcDirFile);
        // check if it exists  srcDirFile
        ASSERT_FALSE(std::filesystem::exists(srcDirFile));
        ctx.request_stop();
    }));

    ctx.run();
}

TEST_F(ManagerTest, testDataDeletePathFile)
{
    using namespace std::literals;
    namespace extData = data_sync::ext_data;

    std::unique_ptr<extData::ExternalDataIFaces> extDataIface =
        std::make_unique<extData::MockExternalDataIFaces>();

    extData::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<extData::MockExternalDataIFaces*>(extDataIface.get());

    ON_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillByDefault([&mockExtDataIfaces]() -> sdbusplus::async::task<> {
        mockExtDataIfaces->setBMCRole(extData::BMCRole::Active);
        co_return;
    });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcPos())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Files",
         {{{"Path",
            ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/TestFile"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/TestFile"},
           {"Description", "File to test immediate sync on self delete"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    std::string srcPath{jsonData["Files"][0]["Path"]};
    std::string destPath{jsonData["Files"][0]["DestinationPath"]};

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    // Create directories in source and destination
    std::filesystem::create_directory(ManagerTest::tmpDataSyncDataDir /
                                      "srcDir");
    std::filesystem::create_directory(ManagerTest::tmpDataSyncDataDir /
                                      "destDir");

    std::string data{"Src: Initial Data\n"};
    // Just for create path in the dest to watch for expectation check
    std::string destData{"Dest: Initial Data\n"};
    ManagerTest::writeData(srcPath, data);
    ManagerTest::writeData(destPath, destData);
    ASSERT_EQ(ManagerTest::readData(srcPath), data);
    ASSERT_EQ(ManagerTest::readData(destPath), destData);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Watch for dest path data change
    data_sync::watch::inotify::DataWatcher dataWatcher(
        ctx, IN_NONBLOCK, IN_DELETE_SELF, destPath,
        std::optional<std::vector<std::filesystem::path>>{},
        std::optional<std::vector<std::filesystem::path>>{});
    ctx.spawn(dataWatcher.onDataChange() |
              sdbusplus::async::execution::then(
                  [&destPath]([[maybe_unused]] const auto& dataOps) {
        EXPECT_FALSE(std::filesystem::exists(destPath));
    }));

    // Remove file after 1s so that the background sync events will be ready
    // to catch.
    ctx.spawn(sdbusplus::async::sleep_for(ctx, 1s) |
              sdbusplus::async::execution::then([&ctx, &srcPath]() {
        // remove the file
        std::filesystem::remove(srcPath);
        ASSERT_FALSE(std::filesystem::exists(srcPath));
        ctx.request_stop();
    }));

    ctx.run();
}

/*
 * Test if the sync is triggered when Disable Sync DBus property is changed
 * from True to False, i.e. sync is changed from disabled to enabled.
 * Will be also testing that the DBus SyncEventsHealth property changes from
 * 'Paused' to 'Ok' when Disable Sync property changes from True to False.
 */
TEST_F(ManagerTest, testDataChangeWhenSyncIsDisabled)
{
    using namespace std::literals;
    using SyncEventsHealth = sdbusplus::common::xyz::openbmc_project::control::
        SyncBMCData::SyncEventsHealth;
    namespace extData = data_sync::ext_data;

    std::unique_ptr<extData::ExternalDataIFaces> extDataIface =
        std::make_unique<extData::MockExternalDataIFaces>();

    extData::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<extData::MockExternalDataIFaces*>(extDataIface.get());

    ON_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillByDefault([&mockExtDataIfaces]() -> sdbusplus::async::task<> {
        mockExtDataIfaces->setBMCRole(extData::BMCRole::Active);
        co_return;
    });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcPos())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Files",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile2"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destFile2"},
           {"Description", "File to test immediate sync when sync is disabled"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    std::string srcPath{jsonData["Files"][0]["Path"]};
    std::string destPath{jsonData["Files"][0]["DestinationPath"]};

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Src: Initial Data\n"};
    // Just for create path in the dest to watch for expectation check
    std::string destData{"Dest: Initial Data\n"};
    ManagerTest::writeData(srcPath, data);
    ManagerTest::writeData(destPath, destData);
    ASSERT_EQ(ManagerTest::readData(srcPath), data);
    ASSERT_EQ(ManagerTest::readData(destPath), destData);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};
    manager.setDisableSyncStatus(true); // disabled the sync events

    EXPECT_NE(ManagerTest::readData(destPath), data)
        << "The data should not match because the manager is spawned and"
        << " sync is disabled.";

    std::string dataToWrite{"Data is modified"};
    std::string dataToStopEvent{"Close spawned inotify event."};
    EXPECT_EQ(manager.getSyncEventsHealth(), SyncEventsHealth::Paused)
        << "SyncEventsHealth should be Paused, as sync is disabled.";

    // write data to src path to create inotify event so that the spawned
    // process is closed.
    ctx.spawn(sdbusplus::async::sleep_for(ctx, 0.1s) |
              sdbusplus::async::execution::then([&srcPath, &dataToStopEvent]() {
        ManagerTest::writeData(srcPath, dataToStopEvent);
    }));

    ctx.spawn(sdbusplus::async::sleep_for(ctx, 0.5s) |
              sdbusplus::async::execution::then(
                  [&destPath, &dataToStopEvent, &manager]() {
        EXPECT_NE(ManagerTest::readData(destPath), dataToStopEvent)
            << "The data should not match as sync is disabled even though"
            << " sync should take place at every data change.";
        manager.setDisableSyncStatus(false); // Trigger the sync events
    }));

    // Write data after 1s so that the background sync events will be ready
    // to catch.
    ctx.spawn(
        sdbusplus::async::sleep_for(ctx, 1s) |
        sdbusplus::async::execution::then([&ctx, &srcPath, &dataToWrite]() {
        ManagerTest::writeData(srcPath, dataToWrite);
        ctx.request_stop();
    }));
    ctx.run();
    EXPECT_EQ(manager.getSyncEventsHealth(), SyncEventsHealth::Ok)
        << "SyncEventsHealth should be Ok, as sync was enabled.";
    EXPECT_EQ(ManagerTest::readData(destPath), dataToWrite)
        << "The data should match with the data as the src was modified"
        << " and sync should take place at every modification.";
}

TEST_F(ManagerTest, testDataCreateInSubDir)
{
    using namespace std::literals;
    namespace extData = data_sync::ext_data;

    std::unique_ptr<extData::ExternalDataIFaces> extDataIface =
        std::make_unique<extData::MockExternalDataIFaces>();

    extData::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<extData::MockExternalDataIFaces*>(extDataIface.get());

    ON_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillByDefault([&mockExtDataIfaces]() -> sdbusplus::async::task<> {
        mockExtDataIfaces->setBMCRole(extData::BMCRole::Active);
        co_return;
    });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcPos())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description",
            "File to test immediate sync on non existent dest path"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    std::string srcDir{jsonData["Directories"][0]["Path"]};
    std::string destDir{jsonData["Directories"][0]["DestinationPath"]};

    // Create directories in source and destination
    std::filesystem::create_directory(srcDir);
    std::filesystem::create_directory(destDir);

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Watch for dest path data change
    data_sync::watch::inotify::DataWatcher dataWatcher(
        ctx, IN_NONBLOCK, IN_CREATE, destDir,
        std::optional<std::vector<std::filesystem::path>>{},
        std::optional<std::vector<std::filesystem::path>>{});
    ctx.spawn(dataWatcher.onDataChange() |
              sdbusplus::async::execution::then(
                  [&destDir]([[maybe_unused]] const auto& dataOps) {
        std::string destSubDir = destDir + "/Test/";
        EXPECT_TRUE(std::filesystem::exists(destSubDir));
    }));

    // Write data after 1s so that the background sync events will be ready
    // to catch.
    ctx.spawn(sdbusplus::async::sleep_for(ctx, 1s) |
              sdbusplus::async::execution::then([&ctx, &srcDir]() {
        std::filesystem::create_directory(srcDir + "/Test");
        ASSERT_TRUE(std::filesystem::exists(srcDir + "/Test/"));
        ctx.request_stop();
    }));

    ctx.run();
}
