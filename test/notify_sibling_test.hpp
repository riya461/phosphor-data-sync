// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "notify_sibling.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

class NotifySiblingTest : public ::testing::Test
{
  protected:
    // Tear down at the end of TestSuite
    static void TearDownTestSuite()
    {
        // Remove each item from the directory
        for (const auto& entry :
             std::filesystem::directory_iterator(NOTIFY_SIBLING_DIR))
        {
            std::filesystem::remove_all(entry.path());
        }
        std::filesystem::remove(NOTIFY_SIBLING_DIR);
    }
};
