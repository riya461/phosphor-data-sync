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
        lg2::debug("Given path [{PATH}] doesn't exist to watch", "PATH",
                   pathToWatch);

        auto getExistingParentPath = [](fs::path dataPath) {
            while (!fs::exists(dataPath))
            {
                dataPath = dataPath.parent_path();
            }
            return dataPath;
        };

        addToWatchList(getExistingParentPath(pathToWatch),
                       _eventMasksIfNotExists);
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
        return processCloseWrite(receivedEventInfo);
    }
    else if ((std::get<EventMask>(receivedEventInfo) &
              (IN_CREATE | IN_ISDIR)) != 0)
    {
        /**
         * Handle the creation of directories inside a monitoring DIR
         */
        return processCreate(receivedEventInfo);
    }
    return false;
}

bool DataWatcher::processCloseWrite(const EventInfo& receivedEventInfo)
{
    fs::path eventReceivedFor =
        _watchDescriptors.at(std::get<WD>(receivedEventInfo));
    lg2::debug("Processing an IN_CLOSE_WRITE for {PATH}", "PATH",
               eventReceivedFor / std::get<BaseName>(receivedEventInfo));

    if (eventReceivedFor.string().starts_with(_dataPathToWatch.string()))
    {
        // Case 1 : The file configured in the JSON exists and is modified
        // Case 2 : A file got created or modified inside a watching subdir
        return true;
    }
    else if (eventReceivedFor / std::get<BaseName>(receivedEventInfo) ==
             _dataPathToWatch)
    {
        // The configured file in the monitored parent directory has been
        // created, hence monitor the configured file and remove the parent
        // watcher as it is no longer needed.
        addToWatchList(_dataPathToWatch, _eventMasksToWatch);
        removeWatch(std::get<WD>(receivedEventInfo));
        return true;
    }
    return false;
}

bool DataWatcher::processCreate(const EventInfo& receivedEventInfo)
{
    fs::path absCreatedPath =
        _watchDescriptors.at(std::get<WD>(receivedEventInfo)) /
        std::get<BaseName>(receivedEventInfo);

    // Process IN_CREATE only for DIR and skip for files as
    // all the file events are handled using IN_CLOSE_WRITE
    if ((std::get<EventMask>(receivedEventInfo) & IN_ISDIR) != 0)
    {
        lg2::debug("Processing an IN_CREATE for {PATH}", "PATH",
                   absCreatedPath);
        if (absCreatedPath.string().starts_with(_dataPathToWatch.string()) &&
            (absCreatedPath != _dataPathToWatch))
        {
            // The created dir is a child directory inside the configured data
            // path add watch for the created child subdirectories.
            createWatchers(absCreatedPath);
            return true;
        }
        else if (_dataPathToWatch.string().starts_with(absCreatedPath.string()))
        {
            // Was monitoring existing parent path of the configured data path
            // and a new file/directory got created inside it.

            auto modifyWatchIfExpected = [this](const fs::path& entry) {
                if (_dataPathToWatch.string().starts_with(entry.string()))
                {
                    // Created DIR is in the tree of the configured path.
                    // Hence, Add watch for the created DIR and remove its
                    // parent watch until the JSON configured DIR creates.
                    if (_dataPathToWatch == entry)
                    {
                        // Add configured event masks if created DIR is the
                        // configured path.
                        addToWatchList(entry, _eventMasksToWatch);
                    }
                    else
                    {
                        addToWatchList(entry, _eventMasksIfNotExists);
                    }

                    // "/a/b/c/d". Here "d" is directory and is the cfg path.
                    // Watching "a" since the "b/c/d" path is not exist.
                    // Now, assume "mkdir -p /a/b/c" happened.
                    // Remove watches for "a" and "b" and watch only "c" parent.
                    // Inotify event received for "a" only and "b" and "c"
                    // created in a single shot. So on recursive directory
                    // iteration, in order to remove the watch for the parents
                    // 'a' and 'b', need to iterate through the map, as only WD
                    // of 'a' is known from the inotify event.
                    if (auto parent =
                            std::ranges::find_if(_watchDescriptors,
                                                 [&entry](const auto& wd) {
                        return (wd.second == entry.parent_path());
                    });
                        parent != _watchDescriptors.end())
                    {
                        removeWatch(parent->first);
                    }
                    return true;
                }
                else if (entry.string().starts_with(_dataPathToWatch.string()))
                {
                    // Created DIR is expected only, and is a child directory of
                    // the configured path.
                    // Hence add watch for created dirs and don't remove it's
                    // parent as created DIRs are childs of the configured DIR.
                    addToWatchList(entry, _eventMasksToWatch);
                    return true;
                }
                return false;
            };
            auto monitoringPath =
                _watchDescriptors.at(std::get<WD>(receivedEventInfo));
            std::ranges::for_each(
                fs::recursive_directory_iterator(monitoringPath),
                modifyWatchIfExpected);
            return true;
        }
    }
    return false;
}

void DataWatcher::removeWatch(int wd)
{
    fs::path pathToRemove = _watchDescriptors.at(wd);

    inotify_rm_watch(_inotifyFileDescriptor(), wd);
    _watchDescriptors.erase(wd);

    lg2::debug("Stopped monitoring {PATH}", "PATH", pathToRemove);
}

} // namespace data_sync::watch::inotify
