// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: the real file system with a fault that can be placed on any
// one operation, and a log of what was asked for. Opening, each read, creating,
// each write and each flush of a file are operations of their own.
#ifndef LINA_FORMAT_TESTS_SUPPORT_FAULTY_FILE_SYSTEM_HPP
#define LINA_FORMAT_TESTS_SUPPORT_FAULTY_FILE_SYSTEM_HPP

#include "lina/format/file_system.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lina::format::testing {

enum class FaultMode : std::uint8_t {
  // One operation fails; everything after it works. This is a full disk or a
  // permission problem. A write that fails leaves the first half of its bytes
  // in the file, as a real one may.
  FailOnce,
  // One operation fails and so does every one after it. This is the process
  // dying at that point: nothing more reaches the disk.
  Crash,
  // One operation throws `std::bad_alloc` instead of returning.
  Throw,
};

class FaultyFileSystem final : public FileSystem {
public:
  // Paths in the log are relative to `root`.
  explicit FaultyFileSystem(std::filesystem::path root)
      : root_(std::move(root)) {}

  // Places the fault on the operation with this number, counting from zero
  // from now on, and empties the log.
  void failAt(std::size_t operation, FaultMode mode);
  // Removes the fault and empties the log.
  void heal();

  // Called after every operation that was let through, with its log entry:
  // the place for a test to act as someone else using the folder meanwhile.
  void afterEach(std::function<void(const std::string &)> action) {
    afterEach_ = std::move(action);
  }

  // True once the fault has been hit.
  [[nodiscard]] bool faultHit() const { return hit_; }
  // The operations asked for since the fault was placed or removed, as
  // "<verb> <path>", the failed ones included.
  [[nodiscard]] const std::vector<std::string> &log() const { return log_; }

  std::expected<std::unique_ptr<InputFile>, Error>
  openForReading(const std::filesystem::path &file, Links links) override;
  std::expected<std::unique_ptr<OutputFile>, Error>
  createFile(const std::filesystem::path &file,
             std::optional<std::uint32_t> permissions) override;
  std::expected<void, Error> rename(const std::filesystem::path &from,
                                    const std::filesystem::path &to) override;
  std::expected<void, Error>
  removeFile(const std::filesystem::path &file) override;
  std::expected<void, Error>
  removeDirectory(const std::filesystem::path &folder) override;
  std::expected<void, Error>
  createDirectory(const std::filesystem::path &folder) override;
  std::expected<void, Error>
  syncDirectory(const std::filesystem::path &folder) override;
  std::expected<FileInfo, Error>
  info(const std::filesystem::path &file) override;
  std::expected<std::vector<std::string>, Error>
  list(const std::filesystem::path &folder) override;
  std::expected<LockAttempt, Error> lock(const std::filesystem::path &file,
                                         std::string_view note) override;

  // Logs an operation and says whether it must fail; throws in `Throw` mode.
  // For the file handles this file system hands out.
  std::optional<Error> enter(std::string_view verb,
                             const std::filesystem::path &path);
  // Tells `afterEach` about the operation just let through.
  void leave();

private:
  // Hands a result through after telling `afterEach_` about it.
  template <class Result> Result done(Result result) {
    leave();
    return result;
  }

  std::filesystem::path root_;
  PosixFileSystem real_;
  std::vector<std::string> log_;
  std::optional<std::size_t> faultAt_;
  FaultMode mode_{FaultMode::FailOnce};
  bool hit_{false};
  std::function<void(const std::string &)> afterEach_;
};

} // namespace lina::format::testing

#endif // LINA_FORMAT_TESTS_SUPPORT_FAULTY_FILE_SYSTEM_HPP
