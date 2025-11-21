// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "notify_service.hpp"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

class NotifyServiceTest : public ::testing::Test
{
  public:
    static void createDummyRqst(const fs::path& fileName,
                                const nlohmann::json& data)
    {
        fs::create_directories(fileName.parent_path());
        std::ofstream out(fileName);
        ASSERT_TRUE(out.is_open()) << "Failed to open " << fileName;
        out << data;
        out.close();
    }

  protected:
    // Tear down at the end of TestSuite
    static void TearDownTestSuite()
    {
        fs::path dir{NOTIFY_SERVICES_DIR};
        fs::remove_all(dir.parent_path());
        fs::remove(dir.parent_path().parent_path());
    }
};
