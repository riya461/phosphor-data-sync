// SPDX-License-Identifier: Apache-2.0

#include "persistent.hpp"

#include "phosphor-logging/lg2.hpp"

#include <format>
#include <fstream>

namespace data_sync::persist
{
std::filesystem::path DBusPropDataFile =
    "/var/lib/phosphor-data-sync/persistence/dbus_props.json";
const std::filesystem::path SyncDisableTimeFile =
    "/var/lib/phosphor-data-sync/persistence/syncDisableTime";

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

std::optional<int64_t> readRawFile(const std::filesystem::path& path)
{
    std::ifstream stream{path};
    if (!stream.is_open())
    {
        return std::nullopt;
    }

    int64_t rawData{};
    stream >> rawData;
    if (stream.fail())
    {
        return std::nullopt;
    }

    return rawData;
}

void writeRawFile(const std::filesystem::path& path, int64_t rawData)
{
    std::ofstream stream{path};
    if (!stream.is_open())
    {
        throw std::runtime_error{
            std::format("Failed to open the file: {}", path.string())};
    }

    stream << rawData;
    if (stream.fail())
    {
        throw std::runtime_error{
            std::format("Failed to write data to the file: {}", path.string())};
    }
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
