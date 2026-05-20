// SPDX-License-Identifier: Apache-2.0

#include <CLI/CLI.hpp>

#include <print>

int main(int argc, char* argv[])
{
    CLI::App app{
        "Data Sync Tool - Command line utility for phosphor-data-sync"};

    if (argc == 1)
    {
        std::println("{}", app.help());
        return 0;
    }

    CLI11_PARSE(app, argc, argv);

    return 0;
}
