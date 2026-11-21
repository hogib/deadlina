// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: folders and files under the build directory's scratch folder.
#ifndef LINA_FORMAT_TESTS_SUPPORT_SCRATCH_HPP
#define LINA_FORMAT_TESTS_SUPPORT_SCRATCH_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lina::format::testing {

// An empty folder for one test case. `name` must be the test case's own:
// test cases run side by side.
std::filesystem::path freshScratch(std::string_view name);

std::string readText(const std::filesystem::path &file);
void writeText(const std::filesystem::path &file, std::string_view text);

// Every file under a folder, as paths relative to it, sorted.
std::vector<std::string> filesUnder(const std::filesystem::path &folder);
// Those of them that are temporary files.
std::vector<std::string>
temporaryFilesUnder(const std::filesystem::path &folder);

// Sets the time a file was last modified, in seconds on an arbitrary scale.
void setModified(const std::filesystem::path &file, std::int64_t seconds);

std::filesystem::path goldenFile(std::string_view relative);

} // namespace lina::format::testing

#endif // LINA_FORMAT_TESTS_SUPPORT_SCRATCH_HPP
