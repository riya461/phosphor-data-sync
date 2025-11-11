// SPDX-License-Identifier: Apache-2.0

#include "error_log.hpp"

#include <unistd.h>

#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>

#include <sstream>

namespace data_sync
{
namespace error_log
{

using namespace phosphor::logging;

FFDCFile::FFDCFile(FFDCFormat format, FFDCSubType subType, FFDCVersion version,
                   const std::string& data) :
    _format(format), _subType(subType), _version(version),
    _fileName("/tmp/syncDataFFDCFile.XXXXXX"), _fd(-1), _data(data)
{
    prepareFFDCFile();
}

FFDCFile::~FFDCFile()
{
    removeFFDCFile();
}

void FFDCFile::prepareFFDCFile()
{
    createFFDCFile();
    writeFFDCData();
    resetFFDCFileSeekPos();
}

void FFDCFile::createFFDCFile()
{
    _fd = mkstemp(_fileName.data());

    if (_fd == -1)
    {
        lg2::error("Failed to create FFDC file {FILE_NAME}", "FILE_NAME",
                   _fileName);
        throw std::runtime_error("Failed to create FFDC file");
    }
}

void FFDCFile::writeFFDCData()
{
    ssize_t rc = write(_fd, _data.data(), _data.size());

    if (rc == -1)
    {
        lg2::error("Failed to write any FFDC info in the file {FILE_NAME}",
                   "FILE_NAME", _fileName);
        throw std::runtime_error("Failed to write FFDC info");
    }
    else if (rc != static_cast<ssize_t>(_data.size()))
    {
        lg2::error("Failed to write all FFDC info in the file {FILE_NAME}",
                   "FILE_NAME", _fileName);
    }
}

void FFDCFile::resetFFDCFileSeekPos()
{
    if (lseek(_fd, 0, SEEK_SET) == (off_t)-1)
    {
        lg2::error("Failed to set SEEK_SET for FFDC file {FILE_NAME}",
                   "FILE_NAME", _fileName);
        throw std::runtime_error("Failed to set SEEK_SET for FFDC file");
    }
}

void FFDCFile::removeFFDCFile()
{
    close(_fd);
    std::remove(_fileName.data());
}

} // namespace error_log
} // namespace data_sync
