// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "external_data_ifaces.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/async.hpp>

namespace data_sync::ext_data
{

/**
 * @class ExternalDataIFacesImpl
 *
 * @brief This class inherits from ExternalDataIFaces to implement
 *        the defined interfaces, enabling seamless use of dependent data
 *        managed by different applications.
 */
class ExternalDataIFacesImpl : public ExternalDataIFaces
{
  public:
    ExternalDataIFacesImpl(const ExternalDataIFacesImpl&) = delete;
    ExternalDataIFacesImpl& operator=(const ExternalDataIFacesImpl&) = delete;
    ExternalDataIFacesImpl(ExternalDataIFacesImpl&&) = delete;
    ExternalDataIFacesImpl& operator=(ExternalDataIFacesImpl&&) = delete;
    ~ExternalDataIFacesImpl() override = default;

    /**
     * @brief The constructor to fetch all external dependent data's
     *
     * @param[in] ctx - The async context
     */
    explicit ExternalDataIFacesImpl(sdbusplus::async::context& ctx);

    /**
     *  @brief API to initiate the systemd reload/restart to the given service.

     *         The operation is considered successful as long as the D-Bus
     *         method call completes successfully. Failures of the reload/
     *         restart on the application side are outside the scope of this
     *         API.
     *
     * @param[in] service - The name of the service to be reloaded/restarted
     * @param[in] method - The method to trigger, can have either "RestartUnit"
     *                     or "ReloadUnit".
     *
     * @return bool - True on success
     *              - False on failure.
     */
    sdbusplus::async::task<bool>
        systemdServiceAction(const std::string& service,
                             const std::string& systemdMethod) override;

    /**
     * @brief Watch for the Redundancy manager properties.
     *
     * The required members will be updated if its changed.
     */
    sdbusplus::async::task<> watchRedundancyMgrProps() override;

  private:
    /**
     * @brief Utility API to get the DBus service name of the given
     *        object path and interface.
     *
     * @param[in] objPath - The object path
     * @param[in] interface - The DBus interface name
     *
     * @return The service name
     */
    sdbusplus::async::task<std::string>
        getDBusService(const std::string& objPath,
                       const std::string& interface);

    /**
     * @brief  Used to retrieve the BMC role from DBus.
     */
    sdbusplus::async::task<> fetchBMCRedundancyMgrProps() override;

    /**
     * @brief Used to retrieve the BMC Position from Dbus.
     */
    sdbusplus::async::task<> fetchBMCPosition() override;

    /**
     * @brief Used to create an error log entry.
     *
     * @param[in] errMsg - Identifier of the error message, as defined in
     *                     the error log message registries.
     * @param[in] errSeverity - Severity level of the error log entry.
     * @param[in] additionalDetails - Optional information to aid in debugging,
     *                                included in the error log entry.
     * @param[in] calloutsDetails - Optional callout details to be included in
     *                              the error log entry.
     */
    sdbusplus::async::task<>
        createErrorLog(const std::string& errMsg, const ErrorLevel& errSeverity,
                       AdditionalData& additionalDetails,
                       const std::optional<json>& calloutsDetails) override;

    /**
     * @brief Used to get the async context
     */
    sdbusplus::async::context& _ctx;
};

} // namespace data_sync::ext_data
