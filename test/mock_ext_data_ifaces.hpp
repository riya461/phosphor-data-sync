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
};

} // namespace data_sync::ext_data
