// SPDX-License-Identifier: Apache-2.0

#include "external_data_ifaces.hpp"

#include <gmock/gmock.h>

namespace data_sync::ext_data
{

class MockExternalDataIFaces : public ExternalDataIFaces
{
  public:
    MockExternalDataIFaces() = default;

    MOCK_METHOD(sdbusplus::async::task<>, fetchBMCRedundancyMgrProps, (),
                (override));
    MOCK_METHOD(sdbusplus::async::task<>, fetchBMCPosition, (), (override));
    MOCK_METHOD(sdbusplus::async::task<>, systemDServiceAction,
                (const std::string&, const std::string&), (override));
    MOCK_METHOD(sdbusplus::async::task<>, createErrorLog,
                (const std::string&, const ErrorLevel&, const nlohmann::json&),
                (override));
    MOCK_METHOD(sdbusplus::async::task<>, watchRedundancyMgrProps, (),
                (override));

    void setBMCRole(const BMCRole& role)
    {
        return bmcRole(role);
    }
    void setBMCRedundancy(const BMCRedundancy& bmcRedundancy)
    {
        return this->bmcRedundancy(bmcRedundancy);
    }
};

} // namespace data_sync::ext_data
