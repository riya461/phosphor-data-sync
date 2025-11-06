// SPDX-License-Identifier: Apache-2.0

#include <nlohmann/json.hpp>
#include <xyz/openbmc_project/Logging/Create/server.hpp>

#include <cstdint>
#include <tuple>
#include <vector>

using json = nlohmann::json;

namespace data_sync
{
namespace error_log
{

/**
 * @brief A tuple which has the info related to the FFDC file.
 *
 * FFDCFormat  - The datatype of FFDC file format.
 * FFDCSubType - The datatype of FFDC file subtype.
 * FFDCVersion - The datatype of FFDC file version.
 * FFDCFileFD  - The datatype of FFDC file descriptor.
 */
using FFDCFormat =
    sdbusplus::xyz::openbmc_project::Logging::server::Create::FFDCFormat;
using FFDCSubType = uint8_t;
using FFDCVersion = uint8_t;
using FFDCFileFD = sdbusplus::message::unix_fd;
using FFDCFileInfo =
    std::tuple<FFDCFormat, FFDCSubType, FFDCVersion, FFDCFileFD>;

/**
 * @brief A vector containing the info of FFDC files for the error log.
 *
 * FFDCFileInfo  - The tuple containing info of FFDC file.
 */
using FFDCFileInfoSet = std::vector<FFDCFileInfo>;

/**
 * @class FFDCFile
 *
 * @brief This class is used to create FFDC file with data.
 */
class FFDCFile
{
  public:
    FFDCFile(const FFDCFile&) = delete;
    FFDCFile& operator=(const FFDCFile&) = delete;
    FFDCFile(FFDCFile&&) = delete;
    FFDCFile& operator=(FFDCFile&&) = delete;

    /**
     * @brief The constructor creates the FFDC file with the given format and
     * data.
     *
     * @param[in] format - The FFDC file format.
     * @param[in] subType - The FFDC file subtype.
     * @param[in] version - The FFDC file version.
     * @param[in] data - The FFDC data to write in the FFDC file.
     */
    FFDCFile(FFDCFormat format, FFDCSubType subType, FFDCVersion version,
             const std::string& data);

    /**
     * @brief Used to remove created FFDC file.
     */
    ~FFDCFile();

    /**
     * @brief Used to get created FFDC file descriptor id.
     *
     * @return file descriptor id
     */
    int getFD()
    {
        return _fd;
    }

    /**
     * @brief Used to get created FFDC file format.
     *
     * @return FFDC file format.
     */
    FFDCFormat getFormat()
    {
        return _format;
    }

    /**
     * @brief Used to get created FFDC file subtype.
     *
     * @return FFDC file subtype.
     */
    FFDCSubType getSubType() const
    {
        return _subType;
    }

    /**
     * @brief Used to get created FFDC file version.
     *
     * @return FFDC file version.
     */
    FFDCVersion getVersion() const
    {
        return _version;
    }

  private:
    /**
     * @brief Function to prepare the FFDC files to pass in the error log
     * request.
     */
    void prepareFFDCFile();

    /**
     * @brief Function to create unique FFDC file.
     *
     * @throws A runtime error on failure
     */
    void createFFDCFile();

    /**
     * @brief Function to write data into the FFDC files.
     *
     * @throws A runtime error on failure
     */
    void writeFFDCData();

    /**
     * @brief Function to set the FFDC file seek position to the begging to
     * consume in the error log.
     *
     * @throws A runtime error on failure
     */
    void resetFFDCFileSeekPos();

    /**
     * @brief Function to remove the create FFDC file.
     */
    void removeFFDCFile();

    /**
     * @brief Stores the FFDC format.
     */
    FFDCFormat _format;

    /**
     * @brief Stores the FFDC subtype.
     */
    FFDCSubType _subType;

    /**
     * @brief Stores the FFDC version.
     */
    FFDCVersion _version;

    /**
     * @brief Stores the unique FFDC file name.
     */
    std::string _fileName;

    /**
     * @brief Stores the created FFDC file descriptor id.
     */
    FFDCFileFD _fd;

    /**
     * @brief Stores the FFDC data and write into the file
     */
    std::string _data;

}; // end of FFDCFile class

} // namespace error_log
} // namespace data_sync
