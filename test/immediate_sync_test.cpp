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
