// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "external_data_ifaces.hpp"

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
     * @brief Used to get the async context
     */
    sdbusplus::async::context& _ctx;
};

} // namespace data_sync::ext_data
