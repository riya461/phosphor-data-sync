// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "external_data_ifaces_impl.hpp"
#include "manager.hpp"
#include "utility.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async/context.hpp>
#include <sdbusplus/server/manager.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/common.hpp>

#include <filesystem>

int main()
{
    namespace fs = std::filesystem;

    using SyncBMCData =
        sdbusplus::common::xyz::openbmc_project::control::SyncBMCData;

    // Create the necessary directories and files if not exists.
    try
    {
        data_sync::utility::setupPaths();
    }
    catch (const std::exception& exc)
    {
        lg2::error(
            "Caught exception while setting up persistent paths, Err : {ERROR}",
            "ERROR", exc);
        exit(EXIT_FAILURE);
    }

    if (!fs::exists(DATA_SYNC_CONFIG_DIR) || fs::is_empty(DATA_SYNC_CONFIG_DIR))
    {
        const fs::path configDirPath{DATA_SYNC_CONFIG_DIR};
        lg2::error(
            "Exiting data-sync, no configurations present in directory {CONFIG_DIR}",
            "CONFIG_DIR", configDirPath);
        return EXIT_FAILURE;
    }

    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t objManager{ctx, SyncBMCData::instance_path};

    data_sync::Manager manager{
        ctx, std::make_unique<data_sync::ext_data::ExternalDataIFacesImpl>(ctx),
        DATA_SYNC_CONFIG_DIR};

    // clang-tidy currently mangles this into something unreadable
    // NOLINTNEXTLINE
    ctx.spawn([](sdbusplus::async::context& ctx) -> sdbusplus::async::task<> {
        ctx.request_name(SyncBMCData::interface);
        co_return;
    }(ctx));

    ctx.run();

    return 0;
}
