#include "manager_test.hpp"

#include <fstream>

std::filesystem::path ManagerTest::dataSyncCfgDir;
std::filesystem::path ManagerTest::tmpDataSyncDataDir;

namespace data_sync
{

TEST_F(ManagerTest, SyncDataHandlesVanishedFileRetry)
{
    using namespace std::chrono_literals;
    namespace ed = data_sync::ext_data;

    auto extIface = std::make_unique<ed::MockExternalDataIFaces>();
    auto* mock = dynamic_cast<ed::MockExternalDataIFaces*>(extIface.get());

    ON_CALL(*mock, fetchBMCRedundancyMgrProps())
        .WillByDefault([mock]() -> sdbusplus::async::task<> {
        mock->setBMCRole(ed::BMCRole::Active);
        co_return;
    });
    ON_CALL(*mock, fetchSiblingBmcPos())
        .WillByDefault([]() -> sdbusplus::async::task<> { co_return; });

    auto srcDir = tmpDataSyncDataDir / "srcDir";
    auto srcSubDir = srcDir / "subSrcDir";
    auto srcFile1 = srcDir / "file1.txt";
    auto srcFile2 = srcSubDir / "file2.txt";
    auto dstDir = tmpDataSyncDataDir / "destDir";

    // Create the parent source directory srcDir and destination directory
    // dstDir, but deliberately avoid creating srcSubDir which contains
    // the target file file2.txt
    //
    // This setup simulates a "vanished file" rsync error (code 24), because
    // rsync will not find the srcFile2 path during the initial sync attempt
    //
    // After triggering this failure, the sync logic is expected to:
    // 1. Parse the vanished file path from rsync output
    // 2. Fall back to the nearest existing parent path (in this case,
    //    srcDir)
    // 3. Retry the sync with that path
    std::filesystem::create_directories(srcDir);
    std::filesystem::create_directories(dstDir);

    std::ofstream outFile(srcFile1);
    ASSERT_TRUE(outFile.is_open());
    outFile << "Temporary content\n";

    ASSERT_TRUE(std::filesystem::exists(srcFile1));
    ASSERT_FALSE(std::filesystem::exists(dstDir / "file1.txt"));

    nlohmann::json cfgJson = {{"Files",
                               {{{"Path", srcFile2.string()},
                                 {"DestinationPath", dstDir.string()},
                                 {"Description", "vanish-retry test"},
                                 {"SyncDirection", "Active2Passive"},
                                 {"RetryAttempts", 2},
                                 {"RetryInterval", "PT1S"},
                                 {"SyncType", "Immediate"}}}}};

    writeConfig(cfgJson);
    config::DataSyncConfig cfg(cfgJson["Files"][0], false);

    sdbusplus::async::context ctx;
    Manager manager(ctx, std::move(extIface), dataSyncCfgDir);

    bool result;
    ctx.spawn(manager.syncData(cfg) |
              sdbusplus::async::execution::then([&](bool ok) {
        result = ok;

        // The retry-logic should succeed after the first vanished failure
        EXPECT_TRUE(result);

        // Because the original file path did not exist, syncData must fall
        // back to its nearest existing parent (srcDir) After a successful
        // retry, that parent directory should now appear inside dstDir
        ASSERT_TRUE(std::filesystem::exists(dstDir / srcDir.relative_path()));
    }));

    // Forcing to trigger inotify events so that all running immediate
    // sync tasks will resume and stop
    ctx.spawn(sdbusplus::async::sleep_for(ctx, 1s) |
              sdbusplus::async::execution::then([&ctx, &srcSubDir]() {
        std::filesystem::create_directories(srcSubDir);
        ctx.request_stop();
    }));

    ctx.run();
}

} // namespace data_sync
