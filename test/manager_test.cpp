// SPDX-License-Identifier: Apache-2.0

#include "manager.hpp"

#include <sdbusplus/async/context.hpp>

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

class ManagerTest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        char tmpdir[] = "/tmp/pdsCfgDirXXXXXX";
        dataSyncCfgDir = mkdtemp(tmpdir);
        std::filesystem::path dataSyncCfgFile{dataSyncCfgDir / "config.json"};
        std::ofstream cfgFile(dataSyncCfgFile);

        jsonData = R"(
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
                            "Periodicity": "PT5M",
                            "RetryAttempts": 1,
                            "RetryInterval": "PT10M",
                            "ExcludeFilesList": ["/directory/file/to/ignore"],
                            "IncludeFilesList": ["/directory/file/to/consider"]
                        }
                    ]
                }
            )"_json;

        cfgFile << jsonData;
    }

    static void TearDownTestSuite()
    {
        std::filesystem::remove_all(dataSyncCfgDir);
        std::filesystem::remove(dataSyncCfgDir);
    }

    static std::filesystem::path dataSyncCfgDir;
    static nlohmann::json jsonData;
};

std::filesystem::path ManagerTest::dataSyncCfgDir;
nlohmann::json ManagerTest::jsonData;

TEST_F(ManagerTest, ParseDataSyncCfg)
{
    using namespace std::literals;

    sdbusplus::async::context ctx;
    data_sync::Manager manager{ctx, ManagerTest::dataSyncCfgDir};
    EXPECT_FALSE(
        manager.containsDataSyncCfg(ManagerTest::jsonData["Files"][0]));

    ctx.spawn(
        sdbusplus::async::sleep_for(ctx, 1ns) |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    EXPECT_TRUE(manager.containsDataSyncCfg(ManagerTest::jsonData["Files"][0]));
}
