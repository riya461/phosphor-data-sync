// SPDX-License-Identifier: Apache-2.0

#include "data_watcher.hpp"

#include <phosphor-logging/lg2.hpp>

#include <cstring>
#include <string>

namespace data_sync::watch::inotify
{

DataWatcher::DataWatcher(sdbusplus::async::context& ctx, const int inotifyFlags,
                         const uint32_t eventMasksToWatch,
                         const fs::path& dataPathToWatch) :
    _inotifyFlags(inotifyFlags), _eventMasksToWatch(eventMasksToWatch),
    _dataPathToWatch(dataPathToWatch), _inotifyFileDescriptor(inotifyInit()),
    _fdioInstance(
        std::make_unique<sdbusplus::async::fdio>(ctx, _inotifyFileDescriptor()))
{
    if (!std::filesystem::exists(_dataPathToWatch))
    {
        lg2::debug("Given path [{PATH}] doesn't exist to watch", "PATH",
                   _dataPathToWatch);
        // TODO : Handle not exists sceanrio by monitoring parent
    }

    createWatchers(_dataPathToWatch);
}

DataWatcher::~DataWatcher()
{
    if (_inotifyFileDescriptor() >= 0)
    {
        std::ranges::for_each(_watchDescriptors, [this](const auto& wd) {
            if (wd.first >= 0)
            {
                inotify_rm_watch(_inotifyFileDescriptor(), wd.first);
            }
        });
    }
}

int DataWatcher::inotifyInit() const
{
    auto fd = inotify_init1(_inotifyFlags);

    if (-1 == fd)
    {
        lg2::error("inotify_init1 call failed with ErrNo : {ERRNO}, ErrMsg : "
                   "{ERRMSG}",
                   "ERRNO", errno, "ERRMSG", strerror(errno));

        // TODO: Throw meaningful exception
        throw std::runtime_error("inotify_init1 failed");
    }
    return fd;
}

void DataWatcher::addToWatchList(const fs::path& pathToWatch,
                                 uint32_t eventMasksToWatch)
{
    auto wd = inotify_add_watch(_inotifyFileDescriptor(), pathToWatch.c_str(),
                                eventMasksToWatch);
    if (-1 == wd)
    {
        lg2::error(
            "inotify_add_watch call failed for {PATH} with ErrNo : {ERRNO}, "
            "ErrMsg : {ERRMSG}",
            "PATH", pathToWatch, "ERRNO", errno, "ERRMSG", strerror(errno));
        // TODO: create error log ? bcoz not watching the  path
        throw std::runtime_error("Failed to add to watch list");
    }
    else
    {
        lg2::debug("Watch added. PATH : {PATH}, wd : {WD}", "PATH", pathToWatch,
                   "WD", wd);
        _watchDescriptors.emplace(wd, pathToWatch);
    }
}

void DataWatcher::createWatchers(const fs::path& pathToWatch)
{
    auto pathToWatchExist = fs::exists(pathToWatch);
    if (pathToWatchExist)
    {
        addToWatchList(pathToWatch, _eventMasksToWatch);
        /* Add watch for subdirectories also if path is a directory
         */
        if (fs::is_directory(pathToWatch))
        {
            auto addWatchIfDir = [this](const fs::path& entry) {
                if (fs::is_directory(entry))
                {
                    addToWatchList(entry, _eventMasksToWatch);
                }
            };
            std::ranges::for_each(fs::recursive_directory_iterator(pathToWatch),
                                  addWatchIfDir);
        }
    }
    else
    {
        // TODO : If configured path not exist, monitor parent until it creates
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<bool> DataWatcher::onDataChange()
{
    // NOLINTNEXTLINE
    co_await _fdioInstance->next();

    if (auto receivedEvents = readEvents(); receivedEvents.has_value())
    {
        auto matched = [this](const auto& event) {
            return processEvent(event);
        };

        co_return std::ranges::any_of(receivedEvents.value(), matched);
    }
    co_return false;
}

std::optional<std::vector<EventInfo>> DataWatcher::readEvents()
{
    // Maximum inotify events supported in the buffer
    constexpr auto maxBytes = sizeof(struct inotify_event) + NAME_MAX + 1;
    uint8_t buffer[maxBytes];
    std::memset(buffer, '\0', maxBytes);

    auto bytes = read(_inotifyFileDescriptor(), buffer, maxBytes);
    if (0 > bytes)
    {
        // Failed to read inotify event
        lg2::error("Failed to read inotify event");
        return std::nullopt;
    }

    auto offset = 0;
    std::vector<EventInfo> receivedEvents{};
    while (offset < bytes)
    {
        // NOLINTNEXTLINE to avoid cppcoreguidelines-pro-type-reinterpret-cast
        auto* receivedEvent = reinterpret_cast<inotify_event*>(&buffer[offset]);

        receivedEvents.emplace_back(receivedEvent->wd, receivedEvent->name,
                                    receivedEvent->mask);

        offset += offsetof(inotify_event, name) + receivedEvent->len;
    }
    return receivedEvents;
}

bool DataWatcher::processEvent(const EventInfo& receivedEventInfo)
{
    if ((std::get<EventMask>(receivedEventInfo) & IN_CLOSE_WRITE) != 0)
    {
        /**
         * Either of the following case occured
         * Case 1 : Files listed in the config and are already existing has been
         *          modified
         * Case 2 : Files got created inside a watching directory
         */
        lg2::debug("Processing an IN_CLOSE_WRITE for {PATH}", "PATH",
                   _watchDescriptors.at(std::get<WD>(receivedEventInfo)) /
                       std::get<BaseName>(receivedEventInfo));
        return true;
    }
    else if ((std::get<EventMask>(receivedEventInfo) &
              (IN_CREATE | IN_ISDIR)) != 0)
    {
        // add watch for the created child subdirectories
        fs::path subDirPath =
            _watchDescriptors.at(std::get<WD>(receivedEventInfo)) /
            std::get<BaseName>(receivedEventInfo);
        lg2::debug("Processing an IN_CREATE for {PATH}", "PATH", subDirPath);
        createWatchers(subDirPath);
        return true;
    }
    return false;
}

} // namespace data_sync::watch::inotify
