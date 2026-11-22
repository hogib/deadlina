// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_TESTING_SCRIPTED_FILE_SYSTEM_HPP
#define LINA_UI_TESTING_SCRIPTED_FILE_SYSTEM_HPP

#include "lina/format/file_system.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace lina::ui::testing {

/// The real file system, except that taking a lock and creating files can
/// be made to answer what a test wants: a file system without locks, a
/// folder that cannot be written to, a disk that is full.
class ScriptedFileSystem final : public format::FileSystem {
public:
  /// HeldElsewhere: another instance has it, and its note cannot be read.
  enum class Lock : std::uint8_t { Real, Unsupported, ReadOnly, HeldElsewhere };

  void answerLocksWith(Lock answer) { lockAnswer_ = answer; }
  /// From now on every file creation fails as on a full disk.
  void fillDisk() { failCreating(""); }
  /// From now on creating a file whose path contains `fragment` fails.
  void failCreating(std::string fragment) {
    failCreating_ = std::move(fragment);
  }
  /// Creating files works again.
  void stopFailingCreation() { failCreating_.reset(); }
  /// From now on renaming onto a path that contains `fragment` fails.
  void failRenamingTo(std::string fragment) {
    failRenaming_ = std::move(fragment);
  }

  std::expected<std::unique_ptr<format::InputFile>, format::Error>
  openForReading(const std::filesystem::path &file,
                 format::Links links) override;
  std::expected<std::unique_ptr<format::OutputFile>, format::Error>
  createFile(const std::filesystem::path &file,
             std::optional<std::uint32_t> permissions) override;
  std::expected<void, format::Error>
  rename(const std::filesystem::path &from,
         const std::filesystem::path &to) override;
  std::expected<void, format::Error>
  removeFile(const std::filesystem::path &file) override;
  std::expected<void, format::Error>
  removeDirectory(const std::filesystem::path &folder) override;
  std::expected<void, format::Error>
  createDirectory(const std::filesystem::path &folder) override;
  std::expected<void, format::Error>
  syncDirectory(const std::filesystem::path &folder) override;
  std::expected<format::FileInfo, format::Error>
  info(const std::filesystem::path &file) override;
  std::expected<std::vector<std::string>, format::Error>
  list(const std::filesystem::path &folder) override;
  std::expected<format::LockAttempt, format::Error>
  lock(const std::filesystem::path &file, std::string_view note) override;

private:
  Lock lockAnswer_{Lock::Real};
  std::optional<std::string> failCreating_;
  std::optional<std::string> failRenaming_;
  format::PosixFileSystem real_;
};

} // namespace lina::ui::testing

#endif // LINA_UI_TESTING_SCRIPTED_FILE_SYSTEM_HPP
