// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/testing/scripted_file_system.hpp"

namespace lina::ui::testing {

std::expected<std::unique_ptr<format::InputFile>, format::Error>
ScriptedFileSystem::openForReading(const std::filesystem::path &file,
                                   format::Links links) {
  return real_.openForReading(file, links);
}

std::expected<std::unique_ptr<format::OutputFile>, format::Error>
ScriptedFileSystem::createFile(const std::filesystem::path &file,
                               std::optional<std::uint32_t> permissions) {
  if (failCreating_ && file.string().contains(*failCreating_)) {
    return std::unexpected(format::Error{
        .code = format::ErrorCode::Io,
        .where = file.string(),
        .message = "no space left on device",
    });
  }
  return real_.createFile(file, permissions);
}

std::expected<void, format::Error>
ScriptedFileSystem::rename(const std::filesystem::path &from,
                           const std::filesystem::path &to) {
  if (failRenaming_ && to.string().contains(*failRenaming_)) {
    return std::unexpected(format::Error{
        .code = format::ErrorCode::Io,
        .where = to.string(),
        .message = "the file system refused the rename",
    });
  }
  return real_.rename(from, to);
}

std::expected<void, format::Error>
ScriptedFileSystem::removeFile(const std::filesystem::path &file) {
  return real_.removeFile(file);
}

std::expected<void, format::Error>
ScriptedFileSystem::removeDirectory(const std::filesystem::path &folder) {
  return real_.removeDirectory(folder);
}

std::expected<void, format::Error>
ScriptedFileSystem::createDirectory(const std::filesystem::path &folder) {
  return real_.createDirectory(folder);
}

std::expected<void, format::Error>
ScriptedFileSystem::syncDirectory(const std::filesystem::path &folder) {
  return real_.syncDirectory(folder);
}

std::expected<format::FileInfo, format::Error>
ScriptedFileSystem::info(const std::filesystem::path &file) {
  return real_.info(file);
}

std::expected<std::vector<std::string>, format::Error>
ScriptedFileSystem::list(const std::filesystem::path &folder) {
  return real_.list(folder);
}

std::expected<format::LockAttempt, format::Error>
ScriptedFileSystem::lock(const std::filesystem::path &file,
                         std::string_view note) {
  switch (lockAnswer_) {
  case Lock::Unsupported:
    return format::LockAttempt{
        .state = format::LockState::Unsupported,
        .lock = nullptr,
        .note = {},
    };
  case Lock::ReadOnly:
    return std::unexpected(format::Error{
        .code = format::ErrorCode::ReadOnly,
        .where = file.string(),
        .message = "read-only file system",
    });
  case Lock::HeldElsewhere:
    return format::LockAttempt{
        .state = format::LockState::HeldElsewhere,
        .lock = nullptr,
        .note = "\x01 not a note",
    };
  case Lock::Real:
    break;
  }
  return real_.lock(file, note);
}

} // namespace lina::ui::testing
