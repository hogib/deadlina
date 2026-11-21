// SPDX-License-Identifier: GPL-3.0-or-later
//
// The few file operations the project folder is built from, behind an
// interface so that tests can make any one of them fail. Each operation is one
// step of the sequences in docs/design/16-project-file-format-encoding.md.
// Files are read and written in pieces through handles, so that a large media
// file never has to be in memory at once.
#ifndef LINA_FORMAT_FILE_SYSTEM_HPP
#define LINA_FORMAT_FILE_SYSTEM_HPP

#include "lina/format/error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lina::format {

using FileTime = std::chrono::sys_time<std::chrono::nanoseconds>;

enum class FileKind : std::uint8_t { Regular, Directory, SymbolicLink, Other };

struct FileInfo {
  FileKind kind{FileKind::Regular};
  std::uint64_t size{0};
  FileTime modified;
  // The permission bits (the low twelve bits of the mode).
  std::uint32_t permissions{0};
};

// A regular file open for reading. Closed when the object goes.
class InputFile {
public:
  InputFile() = default;
  InputFile(const InputFile &) = delete;
  InputFile &operator=(const InputFile &) = delete;
  InputFile(InputFile &&) = delete;
  InputFile &operator=(InputFile &&) = delete;
  virtual ~InputFile() = default;

  // The size of the file when it was opened.
  [[nodiscard]] virtual std::uint64_t size() const = 0;
  // Reads the next bytes into `buffer` and returns how many; zero at the end.
  virtual std::expected<std::size_t, Error> read(std::span<char> buffer) = 0;
};

// A newly created file open for writing. Closed when the object goes; what
// was written is safe on the disk only after `sync` succeeded.
class OutputFile {
public:
  OutputFile() = default;
  OutputFile(const OutputFile &) = delete;
  OutputFile &operator=(const OutputFile &) = delete;
  OutputFile(OutputFile &&) = delete;
  OutputFile &operator=(OutputFile &&) = delete;
  virtual ~OutputFile() = default;

  // Appends all of `bytes`. After a failure part of them may be in the file.
  virtual std::expected<void, Error> write(std::string_view bytes) = 0;
  // Flushes everything written so far to the disk.
  virtual std::expected<void, Error> sync() = 0;
};

// A lock that is held for as long as the object lives. The operating system
// lets go of it by itself when the process ends, however it ends.
class FileLock {
public:
  FileLock() = default;
  FileLock(const FileLock &) = delete;
  FileLock &operator=(const FileLock &) = delete;
  FileLock(FileLock &&) = delete;
  FileLock &operator=(FileLock &&) = delete;
  virtual ~FileLock() = default;
};

enum class LockState : std::uint8_t {
  // The lock is held by the object returned.
  Acquired,
  // Someone else holds it: another running instance, on this machine or, on
  // a shared file system, another.
  HeldElsewhere,
  // The file system has no locks. Nothing is held; whether to go on without
  // the protection is for the caller, and the user, to decide.
  Unsupported,
};

struct LockAttempt {
  LockState state{LockState::Unsupported};
  // Set for `Acquired` only.
  std::unique_ptr<FileLock> lock;
  // The text in the lock file: the caller's own for `Acquired`, the holder's
  // (as far as it could be read) for `HeldElsewhere`.
  std::string note;
};

// Whether a symbolic link at the last step of a path is followed.
enum class Links : std::uint8_t { Refuse, Follow };

class FileSystem {
public:
  FileSystem() = default;
  FileSystem(const FileSystem &) = delete;
  FileSystem &operator=(const FileSystem &) = delete;
  FileSystem(FileSystem &&) = delete;
  FileSystem &operator=(FileSystem &&) = delete;
  virtual ~FileSystem() = default;

  // Opens a regular file. `NotFound` when nothing is there; `NotAFile` for a
  // folder, a device, a pipe, and for a symbolic link unless links are
  // followed.
  virtual std::expected<std::unique_ptr<InputFile>, Error>
  openForReading(const std::filesystem::path &file, Links links) = 0;

  // Creates a file that must not exist yet (`AlreadyExists` otherwise; a
  // symbolic link counts as existing). With `permissions` the file gets
  // exactly those bits; without, what the process's defaults give.
  virtual std::expected<std::unique_ptr<OutputFile>, Error>
  createFile(const std::filesystem::path &file,
             std::optional<std::uint32_t> permissions) = 0;

  // Moves a file over `to`, replacing it in one step.
  virtual std::expected<void, Error>
  rename(const std::filesystem::path &from,
         const std::filesystem::path &to) = 0;

  // Removing what is not there succeeds.
  virtual std::expected<void, Error>
  removeFile(const std::filesystem::path &file) = 0;
  // The folder must be empty. Removing what is not there succeeds.
  virtual std::expected<void, Error>
  removeDirectory(const std::filesystem::path &folder) = 0;

  // Creating what is already there succeeds. The parent must exist.
  virtual std::expected<void, Error>
  createDirectory(const std::filesystem::path &folder) = 0;

  // Flushes a folder's list of names to the disk, which is what makes a
  // creation or a rename inside it survive a power loss.
  virtual std::expected<void, Error>
  syncDirectory(const std::filesystem::path &folder) = 0;

  // What is at a path, without following a symbolic link there. `NotFound`
  // when nothing is.
  virtual std::expected<FileInfo, Error>
  info(const std::filesystem::path &file) = 0;

  // The names in a folder, sorted by byte value. `NotFound` when the folder is
  // not there.
  virtual std::expected<std::vector<std::string>, Error>
  list(const std::filesystem::path &folder) = 0;

  // Creates the lock file if need be and tries to lock it, without waiting.
  // On success `note` is written into it, for others to read. `ReadOnly` when
  // the folder cannot be written to.
  virtual std::expected<LockAttempt, Error>
  lock(const std::filesystem::path &file, std::string_view note) = 0;
};

// The real thing. The lock is an open-file-description lock (`fcntl` with
// `F_OFD_SETLK`) on the lock file, which the kernel releases when the
// descriptor closes; the lock file is removed when the lock object goes.
class PosixFileSystem final : public FileSystem {
public:
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
};

} // namespace lina::format

#endif // LINA_FORMAT_FILE_SYSTEM_HPP
