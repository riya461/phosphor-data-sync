#include "data_watcher.hpp"
#include "manager_test.hpp"

#include <sdbusplus/async.hpp>

#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

std::filesystem::path ManagerTest::dataSyncCfgDir;
std::filesystem::path ManagerTest::tmpDataSyncDataDir;
nlohmann::json ManagerTest::commonJsonData;
std::filesystem::path ManagerTest::destDir;

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

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCPosition())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Files",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile"},
           {"DestinationPath", ManagerTest::destDir.string()},
           {"Description", "File to test immediate sync upon data write"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcPath{jsonData["Files"][0]["Path"]};
    fs::path destDir{jsonData["Files"][0]["DestinationPath"]};
    fs::path destPath = destDir / fs::relative(srcPath, "/");
    nlohmann::json jsonForDest = {
        {"Path", destPath},
        {"Description", "Json to create an inotify watcher on destPath"},
        {"SyncDirection", "Active2Passive"},
        {"SyncType", "Immediate"}};
    data_sync::config::DataSyncConfig dataSyncCfg(jsonForDest, true);

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
        ctx, IN_NONBLOCK, IN_CLOSE_WRITE, dataSyncCfg);
    ctx.spawn(
        dataWatcher.onDataChange() |
        sdbusplus::async::execution::then(
            [&dataToWrite, &destPath]([[maybe_unused]] const auto& dataOps) {
        EXPECT_EQ(dataToWrite, readData(destPath));
    }));

    // NOLINTNEXTLINE
    auto triggerImmediateSync = [&]() -> sdbusplus::async::task<void> {
        // Write data after 1s so that the background sync events will be ready
        // to catch.
        co_await sdbusplus::async::sleep_for(ctx, std::chrono::seconds(1));

        ManagerTest::writeData(srcPath, dataToWrite);
        EXPECT_EQ(dataToWrite, ManagerTest::readData(srcPath));

        // Force an inotify event so running immediate sync tasks wake up
        // handle the last write, and exit once the context stop is requested
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(100));
        ManagerTest::writeData(srcPath, dataToWrite);
        ctx.request_stop();
        co_return;
    };

    ctx.spawn(triggerImmediateSync());
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

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCPosition())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath", ManagerTest::destDir.string()},
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
    nlohmann::json jsonForDest = {
        {"Path", destDirFile.parent_path()},
        {"Description", "Json to create an inotify watcher on destPath"},
        {"SyncDirection", "Active2Passive"},
        {"SyncType", "Immediate"}};
    data_sync::config::DataSyncConfig dataSyncCfg(jsonForDest, true);

    data_sync::watch::inotify::DataWatcher dataWatcher(ctx, IN_NONBLOCK,
                                                       IN_DELETE, dataSyncCfg);
    ctx.spawn(dataWatcher.onDataChange() |
              sdbusplus::async::execution::then(
                  [&destDirFile]([[maybe_unused]] const auto& dataOps) {
        // the file should not exists
        EXPECT_FALSE(std::filesystem::exists(destDirFile));
    }));

    // NOLINTNEXTLINE
    auto triggerImmediateSync = [&]() -> sdbusplus::async::task<void> {
        // Remove file after 1s so that the background sync events will be ready
        // to catch.
        co_await sdbusplus::async::sleep_for(ctx, std::chrono::seconds(1));

        // remove the file from srcDir
        std::filesystem::remove(srcDirFile);
        // check if it exists  srcDirFile
        EXPECT_FALSE(std::filesystem::exists(srcDirFile));

        // Force an inotify event so running immediate sync tasks wake up
        // handle the last write, and exit once the context stop is requested
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(100));
        ManagerTest::writeData(srcDirFile, "data");

        ctx.request_stop();
        co_return;
    };

    ctx.spawn(triggerImmediateSync());
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

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCPosition())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Files",
         {{{"Path",
            ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/TestFile"},
           {"DestinationPath", ManagerTest::destDir.string()},
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
    nlohmann::json jsonForDest = {
        {"Path", destPath},
        {"Description", "Json to create an inotify watcher on destPath"},
        {"SyncDirection", "Active2Passive"},
        {"SyncType", "Immediate"}};
    data_sync::config::DataSyncConfig dataSyncCfg(jsonForDest, true);

    data_sync::watch::inotify::DataWatcher dataWatcher(
        ctx, IN_NONBLOCK, IN_DELETE_SELF, dataSyncCfg);
    ctx.spawn(dataWatcher.onDataChange() |
              sdbusplus::async::execution::then(
                  [&destPath]([[maybe_unused]] const auto& dataOps) {
        EXPECT_FALSE(std::filesystem::exists(destPath));
    }));

    // NOLINTNEXTLINE
    auto triggerImmediateSync = [&]() -> sdbusplus::async::task<void> {
        // Remove file after 1s so that the background sync events will be ready
        // to catch.
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(10));

        // remove the file
        std::filesystem::remove(srcPath);
        EXPECT_FALSE(std::filesystem::exists(srcPath));

        // Force an inotify event so running immediate sync tasks wake up
        // handle the last write, and exit once the context stop is requested
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(100));
        ManagerTest::writeData(srcPath, "data");

        ctx.request_stop();
        co_return;
    };

    ctx.spawn(triggerImmediateSync());
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

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCPosition())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Files",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcFile2"},
           {"DestinationPath", ManagerTest::destDir.string()},
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

    // NOLINTNEXTLINE
    auto triggerImmediateSync = [&]() -> sdbusplus::async::task<void> {
        // Remove file after 1s so that the background sync events will be ready
        // to catch.
        co_await sdbusplus::async::sleep_for(ctx, std::chrono::seconds(1));
        ManagerTest::writeData(srcPath, dataToWrite);

        // Force an inotify event so running immediate sync tasks wake up
        // handle the last write, and exit once the context stop is requested
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(100));
        ManagerTest::writeData(srcPath, dataToWrite);

        ctx.request_stop();
        co_return;
    };

    ctx.spawn(triggerImmediateSync());
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

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCPosition())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath", ManagerTest::destDir.string()},
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
    nlohmann::json jsonForDest = {
        {"Path", destDir},
        {"Description", "Json to create an inotify watcher on destPath"},
        {"SyncDirection", "Active2Passive"},
        {"SyncType", "Immediate"}};
    data_sync::config::DataSyncConfig dataSyncCfg(jsonForDest, true);

    data_sync::watch::inotify::DataWatcher dataWatcher(ctx, IN_NONBLOCK,
                                                       IN_CREATE, dataSyncCfg);
    // NOLINTNEXTLINE
    auto waitForDataChange = [&]() -> sdbusplus::async::task<void> {
        co_await dataWatcher.onDataChange();
        fs::path destSubDir = destDir / fs::relative(srcDir, "/") / "Test";
        // sleep to get sync and refelet in the dest
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(10));
        EXPECT_TRUE(std::filesystem::exists(destSubDir));

        co_return;
    };

    // NOLINTNEXTLINE
    auto triggerImmediateSync = [&]() -> sdbusplus::async::task<void> {
        // Write data after 1s so that the background sync events will be ready
        // to catch.
        co_await sdbusplus::async::sleep_for(ctx, std::chrono::seconds(1));
        std::filesystem::create_directory(srcDir / "Test");
        EXPECT_TRUE(std::filesystem::exists(srcDir / "Test"));

        // Force an inotify event so running immediate sync tasks wake up
        // handle the last write, and exit once the context stop is requested
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(100));
        std::filesystem::create_directory(srcDir / "data");
        ctx.request_stop();
        co_return;
    };

    ctx.spawn(triggerImmediateSync());
    ctx.spawn(waitForDataChange());

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

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCPosition())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/Dir1/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir1/"},
           {"Description", "Directory to test immediate sync on file move"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"}}}}};

    fs::path srcDir{jsonData["Directories"][0]["Path"]};
    fs::path destDir{jsonData["Directories"][0]["DestinationPath"]};
    fs::path destPath = destDir / fs::relative(srcDir, "/");

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Data written to the file\n"};
    fs::create_directory(srcDir);

    // Create directories to simulate move operation
    // File "Test" is present in dir1 and will get moved to dir2.
    fs::create_directories(srcDir / "dir1");
    fs::create_directories(srcDir / "dir2");
    ManagerTest::writeData(srcDir / "dir1" / "Test", data);
    ASSERT_EQ(ManagerTest::readData(srcDir / "dir1" / "Test"), data);
    ASSERT_FALSE(fs::exists(srcDir / "dir2" / "Test"));

    // Create dest paths
    fs::create_directories(destPath);
    fs::create_directories(destPath / "dir1");
    fs::create_directories(destPath / "dir2");
    ASSERT_TRUE(fs::exists(destPath));
    ManagerTest::writeData(destPath / "dir1" / "Test", data);
    ASSERT_EQ(ManagerTest::readData(destPath / "dir1" / "Test"), data);
    ASSERT_FALSE(fs::exists(destPath / "dir2" / "Test"));

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    // Case : File "Test" will move from dir1 to dir2
    // File "Test" will get delete from destPath1
    // File "Test" will get create at destPath2

    // Watch dest paths for data change
    nlohmann::json jsonForDest1 = {
        {"Path", destPath / "dir1"},
        {"Description", "Json to create an inotify watcher on destPath"},
        {"SyncDirection", "Active2Passive"},
        {"SyncType", "Immediate"}};
    data_sync::config::DataSyncConfig dataSyncCfg1(jsonForDest1, true);

    nlohmann::json jsonForDest2 = {
        {"Path", destPath / "dir2"},
        {"Description", "Json to create an inotify watcher on destPath"},
        {"SyncDirection", "Active2Passive"},
        {"SyncType", "Immediate"}};
    data_sync::config::DataSyncConfig dataSyncCfg2(jsonForDest2, true);

    data_sync::watch::inotify::DataWatcher dataWatcher1(
        ctx, IN_NONBLOCK, IN_DELETE, dataSyncCfg1);
    data_sync::watch::inotify::DataWatcher dataWatcher2(
        ctx, IN_NONBLOCK, IN_CREATE, dataSyncCfg2);

    ctx.spawn(dataWatcher1.onDataChange() |
              sdbusplus::async::execution::then(
                  [&destPath]([[maybe_unused]] const auto& dataOps) {
        EXPECT_FALSE(fs::exists(destPath / "dir1" / "Test"));
    }));

    // NOLINTNEXTLINE
    auto waitForDataChange = [&]() -> sdbusplus::async::task<void> {
        using namespace std::chrono_literals;
        co_await dataWatcher2.onDataChange();
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(10));
        EXPECT_TRUE(fs::exists(destPath / "dir2" / "Test"));
        EXPECT_EQ(ManagerTest::readData(destPath / "dir2" / "Test"), data);

        co_return;
    };

    // NOLINTNEXTLINE
    auto triggerImmediateSync = [&]() -> sdbusplus::async::task<void> {
        // Move file after 1s so that the background sync events will be ready
        // to catch.
        co_await sdbusplus::async::sleep_for(ctx, std::chrono::seconds(1));

        fs::rename(srcDir / "dir1" / "Test", srcDir / "dir2" / "Test");
        EXPECT_FALSE(fs::exists(srcDir / "dir1" / "Test"));
        EXPECT_TRUE(fs::exists(srcDir / "dir2" / "Test"));
        EXPECT_EQ(ManagerTest::readData(srcDir / "dir2" / "Test"), data);

        // Force an inotify event so running immediate sync tasks wake up
        // handle the last write, and exit once the context stop is requested
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(100));
        std::filesystem::create_directory(srcDir / "data");

        ctx.request_stop();
        co_return;
    };

    ctx.spawn(triggerImmediateSync());
    ctx.spawn(waitForDataChange());

    ctx.run();
}

TEST_F(ManagerTest, testExcludeFile)
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

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCPosition())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = {
        {"Directories",
         {{{"Path", ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/"},
           {"DestinationPath",
            ManagerTest::tmpDataSyncDataDir.string() + "/destDir/"},
           {"Description",
            "Test the configured exclude list while immediate sync"},
           {"SyncDirection", "Active2Passive"},
           {"SyncType", "Immediate"},
           {"ExcludeList",
            {ManagerTest::tmpDataSyncDataDir.string() + "/srcDir/fileX"}}}}}};

    fs::path srcDir{jsonData["Directories"][0]["Path"]};
    fs::path destDir{jsonData["Directories"][0]["DestinationPath"]};
    fs::path excludeFile = jsonData["Directories"][0]["ExcludeList"][0];

    // Create directories in source and destination
    std::filesystem::create_directory(srcDir);
    std::filesystem::create_directory(destDir);

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    // Create 2 files inside srcDir
    std::string data1{"Data written to file1"};
    std::string dataExcludeFile{"Data written to excludeFile"};

    fs::path file1 = srcDir / "file1";
    ManagerTest::writeData(file1, data1);
    ASSERT_EQ(ManagerTest::readData(file1), data1);
    ManagerTest::writeData(excludeFile, dataExcludeFile);
    ASSERT_EQ(ManagerTest::readData(excludeFile), dataExcludeFile);

    // Watch dest path for data change
    nlohmann::json jsonForDest = {
        {"Path", destDir},
        {"Description", "Json to create an inotify watcher on destPath"},
        {"SyncDirection", "Active2Passive"},
        {"SyncType", "Immediate"}};
    data_sync::config::DataSyncConfig dataSyncCfg(jsonForDest, true);

    data_sync::watch::inotify::DataWatcher dataWatcher(
        ctx, IN_NONBLOCK, IN_CREATE | IN_CLOSE_WRITE, dataSyncCfg);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    std::string dataToFile1{"Data modified in file1"};
    std::string dataToExcludeFile{"Data modified in ExcludeFile"};

    // NOLINTNEXTLINE
    auto waitForDataChange = [&]() -> sdbusplus::async::task<void> {
        using namespace std::chrono_literals;
        co_await dataWatcher.onDataChange();
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(10));
        EXPECT_TRUE(fs::exists(destDir / fs::relative(file1, "/")));
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(10));
        EXPECT_EQ(ManagerTest::readData(destDir / fs::relative(file1, "/")),
                  dataToFile1)
            << "Data in file1 should modified at dest side";
        EXPECT_FALSE(fs::exists(destDir / fs::relative(excludeFile, "/")))
            << "fileX should excluded while syncing to the dest side";

        co_return;
    };

    // NOLINTNEXTLINE
    auto triggerImmediateSync = [&]() -> sdbusplus::async::task<void> {
        // Move file after 1s so that the background sync events will be ready
        // to catch.
        // NOLINTNEXTLINE
        co_await sdbusplus::async::sleep_for(ctx, std::chrono::seconds(1));

        ManagerTest::writeData(excludeFile, dataToExcludeFile);

        EXPECT_EQ(ManagerTest::readData(excludeFile), dataToExcludeFile);
        ManagerTest::writeData(file1, dataToFile1);
        EXPECT_EQ(ManagerTest::readData(file1), dataToFile1);

        // Force an inotify event so running immediate sync tasks wake up
        // handle the last write, and exit once the context stop is requested
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(100));
        ManagerTest::writeData(file1, dataToFile1);

        ctx.request_stop();
        co_return;
    };

    ctx.spawn(waitForDataChange());
    ctx.spawn(triggerImmediateSync());

    ctx.run();
}
