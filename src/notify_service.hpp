// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "data_sync_config.hpp"
#include "external_data_ifaces_impl.hpp"

#include <sdbusplus/async.hpp>

#include <filesystem>

namespace data_sync::notify
{

namespace fs = std::filesystem;

/**
 * @class NotifyService
 *
 * @brief The class which contains the APIs to process the sibling notification
 *        requests received from the sibling BMC on the local BMC and issue the
 *        necessary notifications to the configured services.
 */
class NotifyService
{
  public:
    using CleanupCallback = std::function<void(NotifyService*)>;

    /**
     * @brief Construct a new Notify Service object
     *
     * @param[in] ctx - The async context object for asynchronous operation
     * @param[in] extDataIfaces - The external data interface object to get
     *                            the external data
     * @param[in] notifyFilePath - The root path of the received notify request
     * @param[in] cleanup - Callback function to remove the object from parent
     *                      container
     */
    NotifyService(sdbusplus::async::context& ctx,
                  data_sync::ext_data::ExternalDataIFaces& extDataIfaces,
                  const fs::path& notifyFilePath, CleanupCallback cleanup);

  private:
    /**
     * @brief API to process the received notification request to all the
     *        configured services if configured mode is systemd
     *
     * @param[in] notifyRqstJson - The reference to the received notify request
     *                          in JSON format
     *
     * @return sdbusplus::async::task<>
     */
    sdbusplus::async::task<>
        sendSystemDNotification(const nlohmann::json& notifyRqstJson);

    /**
     * @brief The API to trigger the notification to the configured service upon
     * receiving the request from the sibling BMC
     *
     * @param notifyFilePath[in] - The root path of the received notify request
     * file.
     *
     * @return void
     */
    sdbusplus::async::task<> init(fs::path notifyFilePath);

    /**
     * @brief The async context object used to perform operations asynchronously
     *        as required.
     */
    sdbusplus::async::context& _ctx;

    /**
     * @brief An external data interface object used to seamlessly retrieve
     *        external dependent data.
     */
    data_sync::ext_data::ExternalDataIFaces& _extDataIfaces;

    /**
     * @brief  Callback function invoked when notification processing
     *         completes to remove the NotifyService object from the
     *         parent container
     */
    CleanupCallback _cleanup;
};

} // namespace data_sync::notify
