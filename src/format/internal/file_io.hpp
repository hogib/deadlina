// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format and its tests: whole-file reading and writing on top
// of the handles of the `FileSystem` interface, and the pieces of the POSIX
// implementation that a test has to reach directly.
#ifndef LINA_FORMAT_INTERNAL_FILE_IO_HPP
#define LINA_FORMAT_INTERNAL_FILE_IO_HPP

#include "lina/format/file_system.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace lina::format::detail {

// The size of the pieces files are copied and hashed in.
inline constexpr std::size_t copyChunkBytes = std::size_t{256} * 1024;

// The whole content of a regular file of at most `maxBytes`; `TooLarge`
// beyond that, checked before reading and again while reading.
std::expected<std::string, Error>
readWholeFile(FileSystem &fileSystem, const std::filesystem::path &file,
              std::uint64_t maxBytes, Links links = Links::Refuse);

// Creates a file that must not exist yet, writes all of `bytes` and flushes
// them to the disk. On failure the file may be left behind, incomplete.
std::expected<void, Error>
writeNewFile(FileSystem &fileSystem, const std::filesystem::path &file,
             std::string_view bytes,
             std::optional<std::uint32_t> permissions = std::nullopt);

// Copies a regular file to a new one, in pieces, and flushes the copy.
std::expected<void, Error> copyFile(FileSystem &fileSystem,
                                    const std::filesystem::path &from,
                                    const std::filesystem::path &to,
                                    std::optional<std::uint32_t> permissions);

// The SHA-256 of a regular file, as 64 hexadecimal digits, read in pieces.
std::expected<std::string, Error> hashFile(FileSystem &fileSystem,
                                           const std::filesystem::path &file);

// What a failed attempt to lock a file means.
enum class LockFailure : std::uint8_t {
  // Someone else holds the lock.
  Held,
  // The file system has no locks.
  Unsupported,
  // Anything else: an error to report.
  Other,
};
LockFailure classifyLockFailure(int errorNumber);

// True for the error numbers that mean "this folder cannot be written to".
bool meansReadOnly(int errorNumber);

// Writes all of `bytes` to an open descriptor. Returns zero, or the error
// number of the call that failed.
int writeAll(int descriptor, std::string_view bytes);

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_FILE_IO_HPP
