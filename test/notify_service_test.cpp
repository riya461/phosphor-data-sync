// SPDX-License-Identifier: Apache-2.0

#include "notify_service_test.hpp"

#include "mock_ext_data_ifaces.hpp"

#include <sdbusplus/async.hpp>

/**
 * @brief Case to test the processing of sibling notification request
 *        if application need to notify to reload via systemd
 */
TEST_F(NotifyServiceTest, TestSystemDReloadNotificationRqst)
{
    namespace extData = data_sync::ext_data;

    sdbusplus::async::context ctx;

    nlohmann::json notifyRqstJson = R"(
    {
    "ModifiedDataPath": "/var/tmp/data-sync/a2p/Host/ID",
    "NotifyInfo": {
        "Method": "Reload",
        "Mode": "Systemd",
        "NotifyServices": ["service1", "service2"]
      }
    })"_json;

    std::unique_ptr<extData::ExternalDataIFaces> extDataIfaces =
        std::make_unique<extData::MockExternalDataIFaces>();

    extData::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<extData::MockExternalDataIFaces*>(extDataIfaces.get());

    EXPECT_CALL(*mockExtDataIfaces,
                systemDServiceAction("service1", "ReloadUnit"))
        .WillOnce([]() -> sdbusplus::async::task<> { co_return; });
    EXPECT_CALL(*mockExtDataIfaces,
                systemDServiceAction("service2", "ReloadUnit"))
        .WillOnce([]() -> sdbusplus::async::task<> { co_return; });

    fs::path notifyRqstFileName = NOTIFY_SERVICES_DIR /
                                  fs::path{"dummyNotifyRqst.json"};

    NotifyServiceTest::createDummyRqst(notifyRqstFileName, notifyRqstJson);

    std::vector<std::unique_ptr<data_sync::notify::NotifyService>> _notifyReqs;

    auto testTask = [&ctx, mockExtDataIfaces, notifyRqstFileName,
                     &_notifyReqs]() -> sdbusplus::async::task<> {
        _notifyReqs.emplace_back(
            std::make_unique<data_sync::notify::NotifyService>(
                ctx, *mockExtDataIfaces, notifyRqstFileName,
                [&_notifyReqs](data_sync::notify::NotifyService* ptr) {
            std::erase_if(_notifyReqs,
                          [ptr](const auto& p) { return p.get() == ptr; });
        }));

        // Waiting to make sure that sibling notification is done with
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(200));

        // Once done, notification request no longer exists in fs
        EXPECT_FALSE(fs::exists(notifyRqstFileName));

        ctx.request_stop();
        co_return;
    };

    ctx.spawn(testTask());

    ctx.run();
}

/**
 * @brief Case to test the processing of sibling notification request
 *        if application need to notify to restart via systemd
 */
TEST_F(NotifyServiceTest, TestSystemDRestartNotificationRqst)
{
    namespace extData = data_sync::ext_data;

    sdbusplus::async::context ctx;

    nlohmann::json notifyRqstJson = R"(
    {
    "ModifiedDataPath": "/var/tmp/data-sync/a2p/Host/ID",
    "NotifyInfo": {
        "Method": "Restart",
        "Mode": "Systemd",
        "NotifyServices": ["Service1"]
    }
    })"_json;

    std::unique_ptr<extData::ExternalDataIFaces> extDataIfaces =
        std::make_unique<extData::MockExternalDataIFaces>();

    extData::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<extData::MockExternalDataIFaces*>(extDataIfaces.get());

    EXPECT_CALL(*mockExtDataIfaces,
                systemDServiceAction("Service1", "RestartUnit"))
        .WillOnce([]() -> sdbusplus::async::task<> { co_return; });

    fs::path notifyRqstFileName = NOTIFY_SERVICES_DIR /
                                  fs::path{"dummyNotifyRqst.json"};

    NotifyServiceTest::createDummyRqst(notifyRqstFileName, notifyRqstJson);

    std::vector<std::unique_ptr<data_sync::notify::NotifyService>> _notifyReqs;
    auto testTask = [&ctx, mockExtDataIfaces, notifyRqstFileName,
                     &_notifyReqs]() -> sdbusplus::async::task<> {
        _notifyReqs.emplace_back(
            std::make_unique<data_sync::notify::NotifyService>(
                ctx, *mockExtDataIfaces, notifyRqstFileName,
                [&_notifyReqs](data_sync::notify::NotifyService* ptr) {
            std::erase_if(_notifyReqs,
                          [ptr](const auto& p) { return p.get() == ptr; });
        }));

        // Waiting to make sure that sibling notification is done with
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(200));

        // Once done, notification request no longer exists in fs
        EXPECT_FALSE(fs::exists(notifyRqstFileName));

        ctx.request_stop();
        co_return;
    };

    ctx.spawn(testTask());

    ctx.run();
}
