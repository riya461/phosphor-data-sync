// SPDX-License-Identifier: Apache-2.0

#include "persistent.hpp"

#include "phosphor-logging/lg2.hpp"

#include <format>
#include <fstream>

namespace data_sync::persist
{
std::filesystem::path DBusPropDataFile =
    "/var/lib/phosphor-data-sync/persistence/dbus_props.json";

std::optional<nlohmann::json> readFile(const std::filesystem::path& path)
{
    if (std::filesystem::exists(path))
    {
        std::ifstream stream{path};
        try
        {
            return nlohmann::json::parse(stream);
        }
        catch (const std::exception& e)
        {
            lg2::error("Error parsing JSON in {FILE}: {ERROR}", "FILE", path,
                       "ERROR", e);
        }
    }

    return std::nullopt;
}

namespace util
{

void writeFile(const nlohmann::json& json, const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream stream{path};
    stream << std::setw(4) << json;
    if (stream.fail())
    {
        throw std::runtime_error{
            std::format("Failed writing {}", path.string())};
    }
}

} // namespace util

} // namespace data_sync::persist
