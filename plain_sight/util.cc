#include "plain_sight/util.h"

#include <fstream>
#include <iterator>

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
} // namespace net_zelcon::plain_sight