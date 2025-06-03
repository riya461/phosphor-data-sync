#include "manager.hpp"
#include "mock_ext_data_ifaces.hpp"

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
        char tmpDataDir[] = "/tmp/pdsDataDirXXXXXX";
        tmpDataSyncDataDir = mkdtemp(tmpDataDir);
        data_sync::persist::DBusPropDataFile = tmpDataSyncDataDir /
                                               "persistentData.json";
    }

    // Set up each individual test
    void SetUp() override
    {
        dataSyncCfgFile = dataSyncCfgDir / "testcase_config.json";
        destDir = tmpDataSyncDataDir.string() + "/destDir/";

        // The data-sync using rsync with --relative, it reconstructs
        // the source path tree and attempts to create destination directory
        // on every rsync. The first call succeeds, but subsequent calls may
        // fail with "File exists (17)". To avoid this, ensure the destination
        // directory is created beforehand, or use a different destination path.
        std::filesystem::create_directories(destDir);
    }

    // Tear down each individual test
    void TearDown() override
    {
        // Remove each item from the directory
        for (const auto& entry :
             std::filesystem::directory_iterator(tmpDataSyncDataDir))
        {
            std::filesystem::remove_all(entry.path());
        }
        std::filesystem::remove(dataSyncCfgFile);
    }

    void writeConfig(const nlohmann::json& jsonData)
    {
        std::ofstream cfgFile(dataSyncCfgFile);
        ASSERT_TRUE(cfgFile.is_open()) << "Failed to open " << dataSyncCfgFile;
        cfgFile << jsonData;
        cfgFile.close();
    }

    static void writeData(const std::string& fileName, const std::string& data)
    {
        std::ofstream out(fileName);
        ASSERT_TRUE(out.is_open()) << "Failed to open " << fileName;
        out << data;
        out.close();
    }

    static std::string readData(const std::string& fileName)
    {
        if (!std::filesystem::exists(fileName))
        {
            return "";
        }
        std::ifstream iFile(fileName);
        std::stringstream content;
        content << iFile.rdbuf();
        iFile.close();
        return content.str();
    }

    static void TearDownTestSuite()
    {
        std::filesystem::remove_all(dataSyncCfgDir);
        std::filesystem::remove(dataSyncCfgDir);
        std::filesystem::remove_all(tmpDataSyncDataDir);
        std::filesystem::remove(tmpDataSyncDataDir);
    }

    static std::filesystem::path dataSyncCfgDir;
    static nlohmann::json commonJsonData;
    static std::filesystem::path tmpDataSyncDataDir;
    static std::filesystem::path destDir;
    std::filesystem::path dataSyncCfgFile;
};
