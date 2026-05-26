// SPDX-License-Identifier: Apache-2.0

#include "config_options.hpp"
#include "dbus_interactions.hpp"

#include <CLI/CLI.hpp>
#include <sdbusplus/async.hpp>

#include <print>

int main(int argc, char* argv[])
{
    CLI::App app{
        "Data Sync Tool - Command line utility for phosphor-data-sync"};

    auto* fullSyncGroup =
        app.add_option_group("Full Sync", "Trigger a full sync to the sibling");

    bool fullSync{false};
    fullSyncGroup->add_flag("-f,--fullSync", fullSync, "Start a full sync");

    auto* statusGroup = app.add_option_group(
        "Status Display", "Display current status of phosphor-data-sync");

    bool showStatus{false};
    app.add_flag("-s,--status", showStatus, "Display the status of data sync");

    bool jsonOutput{false};
    statusGroup->add_flag("-j,--json", jsonOutput, "Display in JSON format");

    auto* syncEnableGroup = app.add_option_group("Sync Enable",
                                                 "Enable or disable sync");

    bool enableSync{false};
    auto* enableOpt = syncEnableGroup->add_flag("-e,--enableSync", enableSync,
                                                "Enable sync");

    bool disableSync{false};
    auto* disableOpt = syncEnableGroup->add_flag("-d,--disableSync",
                                                 disableSync, "Disable sync");

    enableOpt->excludes(disableOpt);

    auto* configGroup = app.add_option_group("Config options",
                                             "Configuration related options");

    bool showConfigPaths{false};
    configGroup->add_flag("-l,--listConfigPaths", showConfigPaths,
                          "List all configured paths for syncing");

    // Parse command line arguments
    if (argc == 1)
    {
        std::println("{}", app.help());
        return 0;
    }

    CLI11_PARSE(app, argc, argv);

    sdbusplus::async::context ctx;

    if (enableSync)
    {
        ctx.spawn(datasynctool::dbus_interactions::setSyncEnabled(ctx, true));
    }

    if (disableSync)
    {
        ctx.spawn(datasynctool::dbus_interactions::setSyncEnabled(ctx, false));
    }

    if (showConfigPaths)
    {
        ctx.spawn(
            datasynctool::config_options::listConfigPaths(ctx, jsonOutput));
    }

    if (showStatus)
    {
        ctx.spawn(
            datasynctool::dbus_interactions::displayStatus(ctx, jsonOutput));
    }

    if (fullSync)
    {
        ctx.spawn(datasynctool::dbus_interactions::startFullSync(ctx));
    }

    ctx.spawn(
        sdbusplus::async::execution::just() |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    return 0;
}
