// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "data_sync_config.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

/*
 * Test when the input JSON contains the details of the file to be synced
 * immediately with no overriding retry attempt and retry interval.
 */
TEST(DataSyncConfigParserTest, TestImmediateFileSyncWithNoRetry)
{
    // JSON object with details of file to be synced.
    const auto configJSON = R"(
        {
            "Path": "/file/path/to/sync",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Active2Passive",
            "SyncType": "Immediate"
        }

    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, false);

    EXPECT_EQ(dataSyncConfig._path, "/file/path/to/sync");
    EXPECT_EQ(dataSyncConfig._isPathDir, false);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Active2Passive);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Immediate);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::nullopt);
    EXPECT_EQ(dataSyncConfig._notifySibling, std::nullopt);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts,
              DEFAULT_RETRY_ATTEMPTS);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}

/*
 * Test when the input JSON contains the details of the file to be synced
 * periodically with overriding retry attempt and retry interval.
 */
TEST(DataSyncConfigParserTest, TestPeriodicFileSyncWithRetry)
{
    // JSON object with details of file to be synced.
    const auto configJSON = R"(
        {
            "Path": "/file/path/to/sync",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Passive2Active",
            "SyncType": "Periodic",
            "Periodicity": "PT1M10S",
            "RetryAttempts": 1,
            "RetryInterval": "PT1M"
        }

    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, false);

    EXPECT_EQ(dataSyncConfig._path, "/file/path/to/sync");
    EXPECT_EQ(dataSyncConfig._isPathDir, false);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Passive2Active);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Periodic);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::chrono::seconds(70));
    EXPECT_EQ(dataSyncConfig._notifySibling, std::nullopt);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts, 1);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(60));
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}

/*
 * Test when the input JSON contains the details of the directory to be synced
 * immediately with no overriding retry attempt and retry interval.
 */
TEST(DataSyncConfigParserTest, TestImmediateDirectorySyncWithNoRetry)
{
    const auto configJSON = R"(
        {
            "Path": "/directory/path/to/sync",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Passive2Active",
            "SyncType": "Immediate",
            "ExcludeList": ["/Path/of/files/must/be/ignored/for/sync"],
            "IncludeList": ["/Path/of/files/must/be/considered/for/sync"]
        }
    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, true);

    EXPECT_EQ(dataSyncConfig._path, "/directory/path/to/sync");
    EXPECT_EQ(dataSyncConfig._isPathDir, true);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Passive2Active);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Immediate);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::nullopt);
    EXPECT_EQ(dataSyncConfig._notifySibling, std::nullopt);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts,
              DEFAULT_RETRY_ATTEMPTS);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    EXPECT_EQ(dataSyncConfig._excludeList->first,
              configJSON["ExcludeList"].get<std::unordered_set<fs::path>>());
    EXPECT_EQ(dataSyncConfig._excludeList->second,
              " --filter='-/ /Path/of/files/must/be/ignored/for/sync'");
    EXPECT_EQ(dataSyncConfig._includeList.value(),
              configJSON["IncludeList"].get<std::unordered_set<fs::path>>());
}

/*
 * Test when the input JSON contains the details of the directory to be synced
 * immediately in bidirectional way  with no overriding retry attempt and retry
 * interval.
 */
TEST(DataSyncConfigParserTest, TestImmediateAndBidirectionalDirectorySync)
{
    const auto configJSON = R"(
        {
            "Path": "/directory/path/to/sync",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Bidirectional",
            "SyncType": "Immediate"
        }
    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, true);

    EXPECT_EQ(dataSyncConfig._path, "/directory/path/to/sync");
    EXPECT_EQ(dataSyncConfig._isPathDir, true);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Bidirectional);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Immediate);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::nullopt);
    EXPECT_EQ(dataSyncConfig._notifySibling, std::nullopt);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts,
              DEFAULT_RETRY_ATTEMPTS);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}

/*
 * Test when the input JSON contains the details of the file to be synced
 * periodically where periodicity is not in expected format of 'PTxHxMxS'
 * Hence Periodicity will set to the default value of 60 seconds.
 */
TEST(DataSyncConfigParserTest, TestFileSyncWithInvalidPeriodicity)
{
    // JSON object with details of file to be synced.
    const auto configJSON = R"(
        {
            "Path": "/file/path/to/sync",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Active2Passive",
            "SyncType": "Periodic",
            "Periodicity": "P1D",
            "RetryAttempts": 1,
            "RetryInterval": "PT1M"
        }

    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, false);

    EXPECT_EQ(dataSyncConfig._path, "/file/path/to/sync");
    EXPECT_EQ(dataSyncConfig._isPathDir, false);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Active2Passive);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Periodic);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::chrono::seconds(60));
    EXPECT_EQ(dataSyncConfig._notifySibling, std::nullopt);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts, 1);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(60));
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}

/*
 * Test when the input JSON contains the details of the file to be synced
 * where RetryInterval is not in expected format of 'PTxHxMxS'
 * Hence retryInterval will set to the default value as defined in config.h.
 */
TEST(DataSyncConfigParserTest, TestFileSyncWithInvalidRetryInterval)
{
    // JSON object with details of file to be synced.
    const auto configJSON = R"(
        {
            "Path": "/file/path/to/sync",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Active2Passive",
            "SyncType": "Periodic",
            "Periodicity": "PT30S",
            "RetryAttempts": 1,
            "RetryInterval": "P1D"
        }

    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, false);

    EXPECT_EQ(dataSyncConfig._path, "/file/path/to/sync");
    EXPECT_EQ(dataSyncConfig._isPathDir, false);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Active2Passive);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Periodic);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::chrono::seconds(30));
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts, 1);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    EXPECT_EQ(dataSyncConfig._notifySibling, std::nullopt);
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}

/*
 * Test when the input JSON contains the details of the file to be synced
 * immediately but with invalid SyncDirection.
 * Hence SyncDirection will set to the default value of 'Active2Passive'.
 */
TEST(DataSyncConfigParserTest, TestFileSyncWithInvalidSyncDirection)
{
    // JSON object with details of file to be synced.
    const auto configJSON = R"(
        {
            "Path": "/file/path/to/sync",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Active-Passive",
            "SyncType": "Immediate"
        }

    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, false);

    EXPECT_EQ(dataSyncConfig._path, "/file/path/to/sync");
    EXPECT_EQ(dataSyncConfig._isPathDir, false);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Active2Passive);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Immediate);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::nullopt);
    EXPECT_EQ(dataSyncConfig._notifySibling, std::nullopt);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts,
              DEFAULT_RETRY_ATTEMPTS);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}

/*
 * Test when the input JSON contains the details of the file to be synced
 * immediately but with invalid SyncType.
 * Hence SyncType will set to the default value of 'Immediate'.
 */
TEST(DataSyncConfigParserTest, TestFileSyncWithInvalidSyncType)
{
    // JSON object with details of file to be synced.
    const auto configJSON = R"(
        {
            "Path": "/file/path/to/sync",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Active-Passive",
            "SyncType": "Non-Periodic"
        }

    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, false);

    EXPECT_EQ(dataSyncConfig._path, "/file/path/to/sync");
    EXPECT_EQ(dataSyncConfig._isPathDir, false);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Active2Passive);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Immediate);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::nullopt);
    EXPECT_EQ(dataSyncConfig._notifySibling, std::nullopt);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts,
              DEFAULT_RETRY_ATTEMPTS);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}

/*
 * Test when the input JSON contains the details of the file to be synced
 * immediately with valid Destination and no overriding retry attempt and
 * retry interval.
 */
TEST(DataSyncConfigParserTest, TestFileSyncWithValidDestination)
{
    // JSON object with details of file to be synced.
    const auto configJSON = R"(
        {
            "Path": "/file/path/to/sync",
            "DestinationPath": "/file/path/to/destination",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Active2Passive",
            "SyncType": "Immediate"
        }

    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, false);

    EXPECT_EQ(dataSyncConfig._path, "/file/path/to/sync");
    EXPECT_EQ(dataSyncConfig._isPathDir, false);
    EXPECT_EQ(dataSyncConfig._destPath, "/file/path/to/destination");
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Active2Passive);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Immediate);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::nullopt);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts,
              DEFAULT_RETRY_ATTEMPTS);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    EXPECT_EQ(dataSyncConfig._notifySibling, std::nullopt);
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}

/*
 * Test when the input JSON contains the details of the directory to be synced
 * immediately in bidirectional way with sibling notification enabled.
 */
TEST(DataSyncConfigParserTest, TestSyncConfigWithSiblingNotify)
{
    const auto configJSON = R"(
        {
            "Path": "/directory/path/to/sync/",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Bidirectional",
            "SyncType": "Immediate",
            "NotifySibling" : {
		        "Mode": "DBus",
		        "NotifyServices": ["service1"]
            }
        }
    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, true);

    EXPECT_EQ(dataSyncConfig._path, "/directory/path/to/sync/");
    EXPECT_EQ(dataSyncConfig._isPathDir, true);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Bidirectional);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Immediate);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::nullopt);
    EXPECT_EQ(dataSyncConfig._notifySibling.value()._paths, std::nullopt);
    EXPECT_EQ(dataSyncConfig._notifySibling.value()
                  ._notifyReqInfo.at("Mode")
                  .get<std::string>(),
              "DBus");
    EXPECT_EQ(dataSyncConfig._notifySibling.value()
                  ._notifyReqInfo.at("NotifyServices")
                  .get<std::vector<std::string>>(),
              (std::vector<std::string>{"service1"}));
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts,
              DEFAULT_RETRY_ATTEMPTS);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}

/*
 * Test when the input JSON contains the details of the directory to be synced
 * immediately in bidirectional way with sibling notification configured for a
 * particular file
 */
TEST(DataSyncConfigParserTest, TestSyncConfigWithSelectivePathSiblingNotify)
{
    const auto configJSON = R"(
        {
            "Path": "/directory/path/to/sync/",
            "Description": "Add details about the data and purpose of the synchronization",
            "SyncDirection": "Bidirectional",
            "SyncType": "Immediate",
            "NotifySibling" : {
		        "NotifyOnPaths" : ["/file/inside/directory/for/notification"],
		        "Mode": "Systemd",
		        "NotifyServices": ["service1","service2"]
            }
        }
    )"_json;

    data_sync::config::DataSyncConfig dataSyncConfig(configJSON, true);

    EXPECT_EQ(dataSyncConfig._path, "/directory/path/to/sync/");
    EXPECT_EQ(dataSyncConfig._isPathDir, true);
    EXPECT_EQ(dataSyncConfig._destPath, std::nullopt);
    EXPECT_EQ(dataSyncConfig._syncDirection,
              data_sync::config::SyncDirection::Bidirectional);
    EXPECT_EQ(dataSyncConfig._syncType, data_sync::config::SyncType::Immediate);
    EXPECT_EQ(dataSyncConfig._periodicityInSec, std::nullopt);
    EXPECT_EQ(dataSyncConfig._notifySibling.value()._paths,
              configJSON["NotifySibling"]["NotifyOnPaths"]
                  .get<std::unordered_set<fs::path>>());
    EXPECT_EQ(dataSyncConfig._notifySibling.value()
                  ._notifyReqInfo.at("Mode")
                  .get<std::string>(),
              "Systemd");
    EXPECT_EQ(dataSyncConfig._notifySibling.value()
                  ._notifyReqInfo.at("NotifyServices")
                  .get<std::vector<std::string>>(),
              (std::vector<std::string>{"service1", "service2"}));
    EXPECT_EQ(dataSyncConfig._retry.value()._retryAttempts,
              DEFAULT_RETRY_ATTEMPTS);
    EXPECT_EQ(dataSyncConfig._retry.value()._retryIntervalInSec,
              std::chrono::seconds(DEFAULT_RETRY_INTERVAL));
    EXPECT_EQ(dataSyncConfig._excludeList, std::nullopt);
    EXPECT_EQ(dataSyncConfig._includeList, std::nullopt);
}
