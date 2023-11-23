#include "plain_sight/util.h"

#include <fstream>
#include <iterator>
#include <sstream>

extern "C" {
#include <libavutil/error.h>
}

namespace net_zelcon::plain_sight {
void read_file(std::vector<std::uint8_t> &dst, std::filesystem::path path) {
    std::ifstream file(path, std::ios::binary);
    file.unsetf(std::ios::skipws); // No white space skipping!
    // find file size
    std::streampos file_size;
    file.seekg(0, std::ios::end);
    file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    // reserve capacity
    dst.reserve(file_size);
    dst.insert(dst.begin(), std::istream_iterator<std::uint8_t>(file),
               std::istream_iterator<std::uint8_t>());
}

void read_file(std::string &dst, const std::filesystem::path &path) {
    std::ifstream file(path);
    if (file) {
        file.seekg(0, std::ios::end);
        std::streampos fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        dst.resize(fileSize);
        file.read(&dst[0], fileSize);
    }
}

std::string libav_error(int error) {
    std::string output(AV_ERROR_MAX_STRING_SIZE, '\0');
    av_make_error_string(output.data(), AV_ERROR_MAX_STRING_SIZE, error);
    return output;
}

} // namespace net_zelcon::plain_sight