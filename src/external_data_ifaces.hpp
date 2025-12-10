// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>
#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Logging/Create/common.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

namespace data_sync::ext_data
{

using RBMC = sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;
using BMCRole = RBMC::Role;
using BMCRedundancy = bool;
using BMCPosition = size_t;

using json = nlohmann::json;
using AdditionalData = std::map<std::string, std::string>;
using Logging = sdbusplus::common::xyz::openbmc_project::logging::Create;
using ErrorLevel =
    sdbusplus::xyz::openbmc_project::Logging::server::Entry::Level;

/**
 * @class ExternalDataIFaces
 *
 * @brief This abstract class defines a set of interfaces to retrieve necessary
 *        information from external sources, such as DBus or FileSystems or
 *        other places which may be hosted or modified by various applications.
 *        It enables seamless use of the required information in the logic
 *        without concern for how or when it is retrieved.
 *
 *        The defined interface can be implemented by a derived class for use
 *        in the product, while a mocked class can be utilized for unit testing.
 */
class ExternalDataIFaces
{
  public:
    ExternalDataIFaces() = default;
    ExternalDataIFaces(const ExternalDataIFaces&) = delete;
    ExternalDataIFaces& operator=(const ExternalDataIFaces&) = delete;
    ExternalDataIFaces(ExternalDataIFaces&&) = delete;
    ExternalDataIFaces& operator=(ExternalDataIFaces&&) = delete;
    virtual ~ExternalDataIFaces() = default;

    /**
     * @brief Start fetching external dependent data's
     */
    sdbusplus::async::task<> startExtDataFetches();

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
    virtual sdbusplus::async::task<bool>
        systemdServiceAction(const std::string& service,
                             const std::string& systemdMethod) = 0;

    /**
     * @brief Used to obtain the BMC role.
     *
     * @return The BMC role
     */
    BMCRole bmcRole() const;

    /**
     * @brief Used to obtain the BMC redundancy flag.
     *
     * @return The BMC redundancy flag
     */
    BMCRedundancy bmcRedundancy() const;

    /**
     * @brief Used to obtain the BMC Position.
     *
     * @return The BMC Position
     */
    const BMCPosition& bmcPosition() const;

    /**
     * @brief Used to obtain the BMC Role in string.
     *
     * @return The BMC Role in string format
     */
    std::string bmcRoleInStr() const;

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
    virtual sdbusplus::async::task<> createErrorLog(
        const std::string& errMsg, const ErrorLevel& errSeverity,
        AdditionalData& additionalDetails,
        const std::optional<json>& calloutsDetails = std::nullopt) = 0;

    /**
     * @brief Watch for the Redundancy manager properties.
     *
     * The required members will be updated if its changed.
     */
    virtual sdbusplus::async::task<> watchRedundancyMgrProps() = 0;

  protected:
    /**
     * @brief Used to retrieve the BMC role.
     */
    virtual sdbusplus::async::task<> fetchBMCRedundancyMgrProps() = 0;

    /**
     * @brief Used to retrieve the BMC Position.
     */
    virtual sdbusplus::async::task<> fetchBMCPosition() = 0;

    /**
     * @brief A utility API to assign the retrieved BMC role.
     *
     * @param[in] bmcRole - The retrieved BMC role.
     *
     * @return None.
     */
    void bmcRole(const BMCRole& bmcRole);

    /**
     * @brief A utility API to assign the retrieved BMC redundancy flag.
     *
     * @param[in] bmcRedundancy - The retrieved BMC redundancy flag.
     *
     * @return None.
     */
    void bmcRedundancy(const BMCRedundancy& bmcRedundancy);

    /**
     * @brief A utility API to assign the retrieved BMC Position.
     *
     * @param[in] bmcPosition - The retrieved BMC Position.
     *
     * @return None.
     */
    void bmcPosition(const BMCPosition& bmcPosition);

  private:
    /**
     * @brief Holds the BMC role.
     */
    BMCRole _bmcRole{BMCRole::Unknown};

    /**
     * @brief Indicates whether BMC redundancy is enabled in the system.
     */
    BMCRedundancy _bmcRedundancy{false};

    /**
     * @brief hold the BMC Position
     */
    BMCPosition _bmcPosition;
};

} // namespace data_sync::ext_data
