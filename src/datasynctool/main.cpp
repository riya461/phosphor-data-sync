// SPDX-License-Identifier: Apache-2.0

#include "dbus_interactions.hpp"

#include <CLI/CLI.hpp>
#include <sdbusplus/async.hpp>

#include <print>

int main(int argc, char* argv[])
{
    CLI::App app{
        "Data Sync Tool - Command line utility for phosphor-data-sync"};

    bool showStatus{false};
    app.add_flag("-s,--status", showStatus, "Display the status of data sync");

    bool jsonOutput{false};
    app.add_flag("-j,--json", jsonOutput, "Display in JSON format");

    if (argc == 1)
    {
        std::println("{}", app.help());
        return 0;
    }

    CLI11_PARSE(app, argc, argv);

    sdbusplus::async::context ctx;

    if (showStatus)
    {
        ctx.spawn(
            datasynctool::dbus_interactions::displayStatus(ctx, jsonOutput));
    }

    ctx.spawn(
        sdbusplus::async::execution::just() |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    return 0;
}
