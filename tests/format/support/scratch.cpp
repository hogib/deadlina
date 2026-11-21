// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/scratch.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>

namespace lina::format::testing {

std::filesystem::path freshScratch(std::string_view name) {
  const auto folder = std::filesystem::path{LINA_FORMAT_SCRATCH_DIR} / name;
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder);
  return folder;
}

std::string readText(const std::filesystem::path &file) {
  std::ifstream in{file, std::ios::binary};
  return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

void writeText(const std::filesystem::path &file, std::string_view text) {
  std::ofstream out{file, std::ios::binary | std::ios::trunc};
  out << text;
}

std::vector<std::string> filesUnder(const std::filesystem::path &folder) {
  std::vector<std::string> files;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator{folder}) {
    if (entry.is_regular_file()) {
      files.push_back(entry.path().lexically_relative(folder).string());
    }
  }
  std::ranges::sort(files);
  return files;
}

std::vector<std::string>
temporaryFilesUnder(const std::filesystem::path &folder) {
  std::vector<std::string> files = filesUnder(folder);
  std::erase_if(
      files, [](const std::string &file) { return !file.ends_with(".tmp"); });
  return files;
}

void setModified(const std::filesystem::path &file, std::int64_t seconds) {
  std::filesystem::last_write_time(
      file, std::filesystem::file_time_type{std::chrono::seconds{seconds}});
}

std::filesystem::path goldenFile(std::string_view relative) {
  return std::filesystem::path{LINA_GOLDEN_DIR} / relative;
}

} // namespace lina::format::testing
