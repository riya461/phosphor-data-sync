// SPDX-License-Identifier: Apache-2.0

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async/context.hpp>
#include <sdbusplus/server/manager.hpp>
#include <xyz/openbmc_project/DataSync/BMCData/common.hpp>

int main()
{
    using BMCDataSync =
        sdbusplus::common::xyz::openbmc_project::data_sync::BMCData;

    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t objManager{ctx, BMCDataSync::namespace_path};

    // clang-tidy currently mangles this into something unreadable
    // NOLINTNEXTLINE
    ctx.spawn([](sdbusplus::async::context& ctx) -> sdbusplus::async::task<> {
        ctx.request_name(BMCDataSync::interface);
        co_return;
    }(ctx));

    ctx.run();

    return 0;
}
