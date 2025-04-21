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

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Files",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "File to test immediate sync upon data write"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcPath{jsonData["Files"][0]["Path"]};
    fs::path destDir{jsonData["Files"][0]["DestinationPath"]};
    fs::path destPath = destDir / fs::relative(srcPath, "/");

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Src: Initial Data\n"};
    ManagerTest::writeData(srcPath, data);
    ASSERT_EQ(ManagerTest::readData(srcPath), data);
    // Create dest path for adding watch.
    std::string destData{"Dest: Initial Data\n"};
    fs::create_directories(destPath.parent_path());
    ASSERT_TRUE(fs::exists(destPath.parent_path()));
    ManagerTest::writeData(destPath, destData);
    ASSERT_EQ(ManagerTest::readData(destPath), destData);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    std::string dataToWrite{"Data is modified"};

    // Watch for dest path data change
    data_sync::watch::inotify::DataWatcher dataWatcher(
        ctx, IN_NONBLOCK, IN_CLOSE_WRITE, destPath);
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

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
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

    fs::path srcDir{jsonData["Directories"][0]["Path"]};
    fs::path destDir{jsonData["Directories"][0]["DestinationPath"]};

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Src: Initial Data\n"};
    fs::create_directory(srcDir);
    fs::path srcDirFile = srcDir / "Test";
    // Write data at the src side.
    ManagerTest::writeData(srcDirFile, data);
    ASSERT_EQ(ManagerTest::readData(srcDirFile), data);

    // Replicate the src folder structure at destination side.
    std::string destData{"Dest: Initial Data\n"};
    fs::path destDirFile = destDir / fs::relative(srcDir, "/") / "Test";
    fs::create_directories(destDirFile.parent_path());
    ASSERT_TRUE(fs::exists(destDirFile.parent_path()));
    // Write data at dest side
    ManagerTest::writeData(destDirFile, destData);
    ASSERT_EQ(ManagerTest::readData(destDirFile), destData);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Watch for dest path data change
    data_sync::watch::inotify::DataWatcher dataWatcher(
        ctx, IN_NONBLOCK, IN_DELETE, destDirFile.parent_path());
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

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Files",
         {{{"Path",
            ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/TestFile"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description", "File to test immediate sync on self delete"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcPath{jsonData["Files"][0]["Path"]};
    fs::path destDir{jsonData["Files"][0]["DestinationPath"]};
    fs::path destPath = destDir / fs::relative(srcPath, "/");

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    // Create directories in source
    fs::create_directory(ManagerTest::tmpDataSyncDataDir / "srcDir");

    std::string data{"Src: Initial Data\n"};
    ManagerTest::writeData(srcPath, data);
    ASSERT_EQ(ManagerTest::readData(srcPath), data);

    // Replicate the src folder structure at destination side.
    std::string destData{"Dest: Initial Data\n"};
    fs::create_directories(destPath.parent_path());
    ASSERT_TRUE(fs::exists(destPath.parent_path()));
    ManagerTest::writeData(destPath, destData);
    ASSERT_EQ(ManagerTest::readData(destPath), destData);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Watch for dest path data change
    data_sync::watch::inotify::DataWatcher dataWatcher(
        ctx, IN_NONBLOCK, IN_DELETE_SELF, destPath);
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
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir"},
           {"Description", "File to test immediate sync when sync is disabled"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcPath{jsonData["Files"][0]["Path"]};
    fs::path destDir{jsonData["Files"][0]["DestinationPath"]};
    fs::path destPath = destDir / fs::relative(srcPath, "/");

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Src: Initial Data\n"};
    ManagerTest::writeData(srcPath, data);
    ASSERT_EQ(ManagerTest::readData(srcPath), data);

    // Replicate the src folder structure at destination side.
    std::string destData{"Dest: Initial Data\n"};
    fs::create_directories(destPath.parent_path());
    ManagerTest::writeData(destPath, destData);
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

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
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

    fs::path srcDir{jsonData["Directories"][0]["Path"]};
    fs::path destDir{jsonData["Directories"][0]["DestinationPath"]};

    // Create directories in source and destination
    std::filesystem::create_directory(srcDir);
    std::filesystem::create_directory(destDir);

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Watch for dest path data change
    data_sync::watch::inotify::DataWatcher dataWatcher(ctx, IN_NONBLOCK,
                                                       IN_CREATE, destDir);
    ctx.spawn(dataWatcher.onDataChange() |
              sdbusplus::async::execution::then(
                  [&destDir, &srcDir]([[maybe_unused]] const auto& dataOps) {
        fs::path destSubDir = destDir / fs::relative(srcDir, "/") / "Test";
        EXPECT_TRUE(std::filesystem::exists(destSubDir));
    }));

    // Write data after 1s so that the background sync events will be ready
    // to catch.
    ctx.spawn(sdbusplus::async::sleep_for(ctx, 1s) |
              sdbusplus::async::execution::then([&ctx, &srcDir]() {
        std::filesystem::create_directory(srcDir / "Test");
        ASSERT_TRUE(std::filesystem::exists(srcDir / "Test"));
        ctx.request_stop();
    }));

    ctx.run();
}

TEST_F(ManagerTest, testFileMoveToAnotherDir)
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

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir1/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir1/"},
           {"Description", "Directory to test immediate sync on file move"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}},
          {{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir2/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir2/"},
           {"Description", "Directory to test immediate sync on file move"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcPath1{jsonData["Directories"][0]["Path"]};
    fs::path srcPath2{jsonData["Directories"][1]["Path"]};
    fs::path destDir1{jsonData["Directories"][0]["DestinationPath"]};
    fs::path destDir2{jsonData["Directories"][1]["DestinationPath"]};
    fs::path destPath1 = destDir1 / fs::relative(srcPath1, "/");
    fs::path destPath2 = destDir2 / fs::relative(srcPath2, "/");

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Src: Initial Data\n"};
    fs::create_directory(srcPath1);
    fs::create_directory(srcPath2);
    ManagerTest::writeData(srcPath1 / "Test", data);
    ASSERT_EQ(ManagerTest::readData(srcPath1 / "Test"), data);
    ASSERT_FALSE(fs::exists(srcPath2 / "Test"));

    // Create dest paths
    std::string destData{"Dest: Initial Data\n"};
    fs::create_directories(destPath1);
    fs::create_directories(destPath2);
    ASSERT_TRUE(fs::exists(destPath1));
    ASSERT_TRUE(fs::exists(destPath2));
    ManagerTest::writeData(destPath1 / "Test", destData);
    ASSERT_EQ(ManagerTest::readData(destPath1 / "Test"), destData);
    ASSERT_FALSE(fs::exists(destPath2 / "Test"));

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Case : File "Test" will move from srcPath1 to srcPath2
    // File "Test" will get delete from destPath1
    // File "Test" will get create at destPath2

    // Watch dest paths for data change
    data_sync::watch::inotify::DataWatcher dataWatcher1(ctx, IN_NONBLOCK,
                                                        IN_DELETE, destPath1);
    data_sync::watch::inotify::DataWatcher dataWatcher2(ctx, IN_NONBLOCK,
                                                        IN_CREATE, destPath2);

    ctx.spawn(dataWatcher1.onDataChange() |
              sdbusplus::async::execution::then(
                  [&destPath1]([[maybe_unused]] const auto& dataOps) {
        EXPECT_FALSE(fs::exists(destPath1 / "Test"));
    }));

    ctx.spawn(dataWatcher2.onDataChange() |
              sdbusplus::async::execution::then(
                  [&data, &destPath2]([[maybe_unused]] const auto& dataOps) {
        EXPECT_TRUE(fs::exists(destPath2 / "Test"));
        EXPECT_EQ(ManagerTest::readData(destPath2 / "Test"), data);
    }));

    // Move file after 1s so that the background sync events will be ready
    // to catch.
    ctx.spawn(sdbusplus::async::sleep_for(ctx, 1s) |
              sdbusplus::async::execution::then(
                  [&ctx, &srcPath1, &srcPath2, &data]() {
        fs::rename(srcPath1 / "Test", srcPath2 / "Test");
        EXPECT_FALSE(fs::exists(srcPath1 / "Test"));
        EXPECT_TRUE(fs::exists(srcPath2 / "Test"));
        ASSERT_EQ(ManagerTest::readData(srcPath2 / "Test"), data);
        ctx.request_stop();
    }));

    ctx.run();
}
