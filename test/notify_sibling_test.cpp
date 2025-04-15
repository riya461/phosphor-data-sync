// SPDX-License-Identifier: Apache-2.0

#include "notify_sibling_test.hpp"

namespace fs = std::filesystem;

/**
 * Test case to verify whether sibling notification request is framing as
 * expected and is able to write into the unique JSON file created
 */
TEST_F(NotifySiblingTest, TestWhenSiblingNotificationEnabled)
{
    const auto configJSON = R"(
        {
            "Path": "/directory/path/to/sync/",
            "Description": "Configuration to test the sibling notification",
            "SyncDirection": "Bidirectional",
            "SyncType": "Immediate",
            "NotifySibling" : {
		        "Mode": "Systemd",
		        "NotifyServices": ["service1","service2"]
            }
        }
    )"_json;

    fs::path modifiedDataPath{"/directory/path/to/sync/testFile"};

    // Parse config and store in the container
    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, true);

    // Create NotifySibling (this should generate the notify file)
    data_sync::notify::NotifySibling notifySibling(dataSyncConfig,
                                                   modifiedDataPath);

    // Get generated notify file path
    auto notifyFilePath = notifySibling.getNotifyFilePath();
    ASSERT_TRUE(fs::exists(notifyFilePath));

    // Read back JSON content from file
    std::ifstream file(notifyFilePath);
    ASSERT_TRUE(file.is_open());

    nlohmann::json notifyRqstJson;
    file >> notifyRqstJson;

    // Expected JSON in the notify file
    const auto expectedJson = R"(
    {
        "ModifiedDataPath": "/directory/path/to/sync/testFile",
        "NotifyInfo": {
            "Mode": "Systemd",
            "NotifyServices": ["service1", "service2"]
        }
    })"_json;

    // Validate the JSON
    EXPECT_EQ(notifyRqstJson, expectedJson);
}

/**
 * Test case to verify whether sibling notification request is framing as
 * expected and is able to write into the unique JSON file created for the paths
 * mentioned in the configured paths for notification.
 */
TEST_F(NotifySiblingTest, TestWhenSiblingNotificationEnabledOnPaths)
{
    const auto configJSON = R"(
        {
            "Path": "/directory/path/to/sync/",
            "Description": "Configuration to test the sibling notification",
            "SyncDirection": "Bidirectional",
            "SyncType": "Immediate",
            "NotifySibling" : {
		        "NotifyOnPaths" : ["/directory/path/to/sync/testFile"],
		        "Mode": "DBus",
		        "NotifyServices": ["service1","service2"]
            }
        }
    )"_json;

    fs::path modifiedDataPath{"/directory/path/to/sync/testFile"};

    // Parse config and store in the container
    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, true);

    // Create NotifySibling (this should generate the notify file)
    data_sync::notify::NotifySibling notifySibling(dataSyncConfig,
                                                   modifiedDataPath);

    // Get generated notify file path
    auto notifyFilePath = notifySibling.getNotifyFilePath();
    ASSERT_TRUE(fs::exists(notifyFilePath));

    // Read back JSON content from file
    std::ifstream file(notifyFilePath);
    ASSERT_TRUE(file.is_open());

    nlohmann::json notifyRqstJson;
    file >> notifyRqstJson;

    // Expected JSON in the notify file
    const auto expectedJson = R"(
    {
        "ModifiedDataPath": "/directory/path/to/sync/testFile",
        "NotifyInfo": {
            "Mode": "DBus",
            "NotifyServices": ["service1", "service2"]
        }
    })"_json;

    // Validate the JSON
    EXPECT_EQ(notifyRqstJson, expectedJson);
}
