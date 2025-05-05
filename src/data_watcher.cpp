// SPDX-License-Identifier: Apache-2.0

#include "data_watcher.hpp"

#include <phosphor-logging/lg2.hpp>

#include <cstring>
#include <string>

namespace data_sync::watch::inotify
{

DataWatcher::DataWatcher(sdbusplus::async::context& ctx, const int inotifyFlags,
                         const uint32_t eventMasksToWatch,
                         const fs::path& dataPathToWatch, const
                         std::optional<std::vector<fs::path>> includeList,
                         std::optional<std::vector<fs::path>> excludeList ) :
    _inotifyFlags(inotifyFlags), _eventMasksToWatch(eventMasksToWatch),
    _dataPathToWatch(dataPathToWatch), _includeList(includeList),
    _excludeList(excludeList), _inotifyFileDescriptor(inotifyInit()),
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

fs::path DataWatcher::getExistingParentPath(const fs::path& dataPath)
{
    fs::path parentPath = dataPath.parent_path();
    while (!fs::exists(parentPath))
    {
        parentPath = parentPath.parent_path();
    }
    return parentPath;
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
                    if (_excludeList.has_value())
                    {
                        auto matchesWithEntry = [&entry](const auto& excludePath)
                        {
                            return (entry/"").string().starts_with(excludePath.string());
                        };
                        if (std::ranges::any_of(_excludeList.value(),
                                matchesWithEntry))
                        {
                            return;
                        }
                    }
                    else if (_includeList.has_value())
                    {
                        auto matchesOrChildOfEntry = [&entry](const auto&
                                                                includePath)
                        {
                            return includePath.string().starts_with(entry.string());
                        };
                        if (std::ranges::none_of(_includeList.value(),
                                matchesOrChildOfEntry))
                        {
                            lg2::debug("{ENTRY} not in includeList", "ENTRY",
                            entry);
                            return;
                        }
                    }
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

        addToWatchList(getExistingParentPath(pathToWatch),
                       _eventMasksIfNotExists);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<DataOperations> DataWatcher::onDataChange()
{
    // NOLINTNEXTLINE
    co_await _fdioInstance->next();

    if (auto receivedEvents = readEvents(); receivedEvents.has_value())
    {
        processEvents(receivedEvents.value());
    }

    co_return _dataOperations;
}

std::optional<std::vector<EventInfo>> DataWatcher::readEvents()
{
    // Before reading the events clear the map of data operation to remove the
    // handled operation details.
    if (!_dataOperations.empty())
    {
        _dataOperations.clear();
    }

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

        lg2::debug("receivedEvent->name : {NAME}", "NAME", receivedEvent->name);
        lg2::debug("receivedEvent->mask : {MASK}({NAME})", "MASK",
            receivedEvent->mask, "NAME", eventName(receivedEvent->mask));
        lg2::debug("receivedEvent->cookie : {COOKIE}", "COOKIE",
        receivedEvent->cookie);
        receivedEvents.emplace_back(receivedEvent->wd, receivedEvent->name,
                                    receivedEvent->mask);

        offset += offsetof(inotify_event, name) + receivedEvent->len;
    }
    return receivedEvents;
}

void DataWatcher::processEvents(
    const std::vector<EventInfo>& receivedEventsInfo)
{
    std::ranges::for_each(receivedEventsInfo, [this](const auto& event) {
        std::optional<DataOperation> dataOperation = processEvent(event);
        if (dataOperation.has_value())
        {
            _dataOperations.insert_or_assign(dataOperation.value().first,
                                             dataOperation.value().second);
        }
    });
}

std::optional<DataOperation>
    DataWatcher::processEvent(const EventInfo& receivedEventInfo)
{
    fs::path eventReceivedFor =
        _watchDescriptors.at(std::get<WD>(receivedEventInfo)) /
        std::get<BaseName>(receivedEventInfo);
    if (fs::is_directory(eventReceivedFor))
    {
        eventReceivedFor /= "";
    }

    if (_excludeList.has_value())
    {
        // Skip the events received for the paths which are in excluded list.
        if (std::ranges::contains(_excludeList.value(), eventReceivedFor))
        {
            lg2::debug("Event received for  {PATH} and is in excludedList",
            "PATH", eventReceivedFor);
            return std::nullopt;
        }
    }
    else if (_includeList.has_value())
    {
        // Skip the events received for the paths which are not in included list
        // if configured.
        auto isIncludePath = [&eventReceivedFor](const auto& includePath)
        {
            return eventReceivedFor.string().starts_with(includePath.string());
        };
        if (std::ranges::none_of(_includeList.value(), isIncludePath))
        {
            lg2::debug("Event received for {PATH} and is not in includeList",
            "PATH", eventReceivedFor);
            return std::nullopt;
        }
    }

    if ((std::get<EventMask>(receivedEventInfo) & IN_CLOSE_WRITE) != 0)
    {
        return processCloseWrite(receivedEventInfo);
    }
    else if ((std::get<EventMask>(receivedEventInfo) &
              (IN_CREATE | IN_ISDIR)) == (IN_CREATE | IN_ISDIR))
    {
        /**
         * Handle the creation of directories inside a monitoring DIR
         */
        return processCreate(receivedEventInfo);
    }
    else if ((std::get<EventMask>(receivedEventInfo) & IN_MOVED_FROM) != 0)
    {
        return processMovedFrom(receivedEventInfo);
    }
    else if ((std::get<EventMask>(receivedEventInfo) & IN_MOVED_TO) != 0)
    {
        return processMovedTo(receivedEventInfo);
    }
    else if ((std::get<EventMask>(receivedEventInfo) & IN_DELETE_SELF) != 0)
    {
        return processDeleteSelf(receivedEventInfo);
    }
    else if ((std::get<EventMask>(receivedEventInfo) & IN_DELETE) != 0)
    {
        return processDelete(receivedEventInfo);
    }
    else
    {
        lg2::debug("Received an uninterested inotify event with mask : {MASK} ",
                   "MASK", std::get<EventMask>(receivedEventInfo));
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<DataOperation>
    DataWatcher::processCloseWrite(const EventInfo& receivedEventInfo)
{
    if (std::get<BaseName>(receivedEventInfo).starts_with("."))
    {
        lg2::debug("Ignoring the IN_CLOSE_WRITE for the hidden file");
        return std::nullopt;
    }

    fs::path eventReceivedFor =
        _watchDescriptors.at(std::get<WD>(receivedEventInfo));
    lg2::debug("Processing an IN_CLOSE_WRITE for {PATH}", "PATH",
               eventReceivedFor / std::get<BaseName>(receivedEventInfo));

    if (eventReceivedFor.string().starts_with(_dataPathToWatch.string()))
    {
        // Case 1 : The file configured in the JSON exists and is modified
        // Case 2 : A file got created or modified inside a watching subdir
        return std::make_pair(_dataPathToWatch, DataOps::COPY);
    }
    else if (fs::equivalent(eventReceivedFor /
                                std::get<BaseName>(receivedEventInfo),
                            _dataPathToWatch))
    {
        // The configured file in the monitored parent directory has been
        // created, hence monitor the configured file and remove the parent
        // watcher as it is no longer needed.
        addToWatchList(_dataPathToWatch, _eventMasksToWatch);
        removeWatch(std::get<WD>(receivedEventInfo));
        return std::make_pair(_dataPathToWatch, DataOps::COPY);
    }

    return std::nullopt;
}

std::optional<DataOperation>
    DataWatcher::processCreate(const EventInfo& receivedEventInfo)
{
    // Process IN_CREATE only for DIR and skip for files as
    // all the file events are handled using IN_CLOSE_WRITE
    if ((std::get<EventMask>(receivedEventInfo) & IN_ISDIR) != 0)
    {
        fs::path absCreatedPath =
            _watchDescriptors.at(std::get<WD>(receivedEventInfo)) /
            std::get<BaseName>(receivedEventInfo) / "";

        lg2::debug("Processing an IN_CREATE for {PATH}", "PATH",
                   absCreatedPath);
        if (absCreatedPath.string().starts_with(_dataPathToWatch.string()) &&
            !fs::equivalent(_dataPathToWatch, absCreatedPath))
        {
            // The created dir is a child directory inside the configured data
            // path add watch for the created child subdirectories.
            createWatchers(absCreatedPath);

            return std::make_pair(absCreatedPath, DataOps::COPY);
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
                    if (fs::equivalent(_dataPathToWatch, entry))
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
                        return (fs::equivalent(wd.second, entry.parent_path()));
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
            return std::make_pair(absCreatedPath, DataOps::COPY);
        }
    }
    return std::nullopt;
}

std::optional<DataOperation>
    DataWatcher::processMovedFrom(const EventInfo& receivedEventInfo)
{
    // Case 1 : A file inside a watching directory is moved to some other
    // directory
    // Case 2 : A file inside a watching directory is renamed to new name.

    fs::path absMovedPath =
        _watchDescriptors.at(std::get<WD>(receivedEventInfo)) /
        std::get<BaseName>(receivedEventInfo);
    if (absMovedPath.string().starts_with(_dataPathToWatch.string()))
    {
        lg2::debug("Processing IN_MOVED_FROM for {PATH}", "PATH", absMovedPath);
        return std::make_pair(absMovedPath, DataOps::COPY);
    }
    return std::nullopt;
}

std::optional<DataOperation>
    DataWatcher::processMovedTo(const EventInfo& receivedEventInfo)
{
    // Case 1 : If a file inside a configured and watching directory is renamed.
    // Case 2 : A file is moved to a configred and watching directory.
    // Case 3 : If a configured and watching file itself is renamed.
    fs::path absCopiedPath =
        _watchDescriptors.at(std::get<WD>(receivedEventInfo)) /
        std::get<BaseName>(receivedEventInfo);

    if (absCopiedPath.string().starts_with(_dataPathToWatch.string()))
    {
        lg2::debug("Processing IN_MOVED_TO for {PATH}", "PATH", absCopiedPath);
        return std::make_pair(absCopiedPath, DataOps::COPY);
    }
    return std::nullopt;
}

std::optional<DataOperation>
    DataWatcher::processDeleteSelf(const EventInfo& receivedEventInfo)
{
    // Case 1 : A monitoring file got deleted.
    // case 2 : A monitoring directory got deleted.

    fs::path deletedPath =
        _watchDescriptors.at(std::get<WD>(receivedEventInfo));
    lg2::debug("Processing IN_DELETE_SELF for {PATH}", "PATH", deletedPath);

    if (_watchDescriptors.size() == 1)
    {
        // If configured file / directory got deleted add a watch on parent
        // dir to notify future create events.

        // Unique watches are there for all the sub directories also. Hence when
        // a configured and monitoring directory deletes, IN_DELETE_SELF will
        // emit for all sub directories which will remove its watches and
        // finally will get IN_DELETE_SELF for the configured dir also which
        // makes the size of _watchDescriptors 1.

        addToWatchList(getExistingParentPath(deletedPath),
                       _eventMasksIfNotExists);
    }

    // Remove the watch for the deleted path
    removeWatch(std::get<WD>(receivedEventInfo));

    return std::make_pair(deletedPath, DataOps::DELETE);
}

std::optional<DataOperation>
    DataWatcher::processDelete(const EventInfo& receivedEventInfo)
{
    fs::path deletedPath =
        _watchDescriptors.at(std::get<WD>(receivedEventInfo)) /
        std::get<BaseName>(receivedEventInfo);

    // Deleting sub directories will emit IN_DELETE_SELF as all the
    // subdirectories have unique watches. Hence skipping IN_DELETE for
    // subdirectories.
    if ((std::get<EventMask>(receivedEventInfo) & IN_ISDIR) == 0)
    {
        // A file inside a monitoring directory got deleted.
        lg2::debug("Processing IN_DELETE for {PATH}", "PATH", deletedPath);
        return std::make_pair(deletedPath, DataOps::DELETE);
    }

    return std::nullopt;
}

void DataWatcher::removeWatch(int wd)
{
    fs::path pathToRemove = _watchDescriptors.at(wd);

    inotify_rm_watch(_inotifyFileDescriptor(), wd);
    _watchDescriptors.erase(wd);

    lg2::debug("Stopped monitoring {PATH}", "PATH", pathToRemove);
}

} // namespace data_sync::watch::inotify
