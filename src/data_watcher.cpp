// SPDX-License-Identifier: Apache-2.0

#include "data_watcher.hpp"

#include <phosphor-logging/lg2.hpp>

#include <cstring>
#include <string>

namespace data_sync::watch::inotify
{

namespace fs = std::filesystem;

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
    _watchDescriptor = addToWatchList();
}

DataWatcher::~DataWatcher()
{
    if ((_inotifyFileDescriptor() >= 0) && (_watchDescriptor >= 0))
    {
        inotify_rm_watch(_inotifyFileDescriptor(), _watchDescriptor);
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

int DataWatcher::addToWatchList()
{
    auto wd = inotify_add_watch(_inotifyFileDescriptor(),
                                _dataPathToWatch.c_str(), _eventMasksToWatch);

    if (-1 == wd)
    {
        lg2::error("inotify_add_watch call failed with ErrNo : {ERRNO}, "
                   "ErrMsg : {ERRMSG}",
                   "ERRNO", errno, "ERRMSG", strerror(errno));
        throw std::runtime_error("Failed to add to watch list");
    }
    else
    {
        lg2::debug("Watch added. PATH : {PATH} wd : {WD}", "PATH",
                   _dataPathToWatch, "WD", wd);
    }
    return wd;
}

// NOLINTNEXTLINE
sdbusplus::async::task<bool> DataWatcher::onDataChange()
{
    // NOLINTNEXTLINE
    co_await _fdioInstance->next();

    if (auto receivedEvents = readEvents(); receivedEvents.has_value())
    {
        auto matched = [](const auto& event) { return processEvent(event); };

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

        receivedEvents.emplace_back(receivedEvent->name, receivedEvent->mask);

        lg2::debug("Received inotify event with mask : {MASK} for {PATH}",
                   "MASK", receivedEvent->mask, "PATH", _dataPathToWatch);
        offset += offsetof(inotify_event, name) + receivedEvent->len;
    }
    return receivedEvents;
}

bool DataWatcher::processEvent(EventInfo receivedEventInfo)
{
    return (std::get<EventMask>(receivedEventInfo) & IN_CLOSE_WRITE) != 0;
}

} // namespace data_sync::watch::inotify
