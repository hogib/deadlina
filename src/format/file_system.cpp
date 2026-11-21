// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/format/file_system.hpp"

#include "internal/file_io.hpp"
#include "lina/format/sha256.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lina::format {

namespace {

// Before the process's mask: readable and writable by all.
constexpr mode_t defaultFileMode = 0666;
constexpr mode_t folderMode = 0777;
constexpr mode_t permissionBits = 07777;
// How often taking the lock starts over when the lock file is replaced
// underneath it by a holder letting go at that moment.
constexpr int lockRounds = 4;
// More than any note an instance writes.
constexpr std::size_t maxNoteBytes = 1024;

// The failure of a system call on a path, from its error number.
Error failure(std::string_view action, const std::filesystem::path &path,
              int errorNumber) {
  ErrorCode code = ErrorCode::Io;
  if (errorNumber == ENOENT) {
    code = ErrorCode::NotFound;
  } else if (errorNumber == EEXIST) {
    code = ErrorCode::AlreadyExists;
  } else if (errorNumber == ELOOP) {
    // What opening without following links says of a symbolic link.
    code = ErrorCode::NotAFile;
  }
  return {
      .code = code,
      .where = path.string(),
      .message = "cannot " + std::string{action} + ": " +
                 std::generic_category().message(errorNumber),
  };
}

// Closes a descriptor when it goes out of scope.
class Descriptor {
public:
  explicit Descriptor(int value) : value_(value) {}
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  Descriptor(Descriptor &&other) noexcept
      : value_(std::exchange(other.value_, -1)) {}
  Descriptor &operator=(Descriptor &&) = delete;
  ~Descriptor() {
    if (value_ >= 0) {
      ::close(value_);
    }
  }

  [[nodiscard]] bool open() const { return value_ >= 0; }
  [[nodiscard]] int get() const { return value_; }

private:
  int value_;
};

std::expected<void, Error> done(int result, std::string_view action,
                                const std::filesystem::path &path) {
  if (result != 0) {
    return std::unexpected(failure(action, path, errno));
  }
  return {};
}

// As `done`, for removals: what is not there counts as removed.
std::expected<void, Error> removed(int result, std::string_view action,
                                   const std::filesystem::path &path) {
  if (result != 0 && errno != ENOENT) {
    return std::unexpected(failure(action, path, errno));
  }
  return {};
}

FileKind kindOf(mode_t mode) {
  if (S_ISREG(mode)) {
    return FileKind::Regular;
  }
  if (S_ISDIR(mode)) {
    return FileKind::Directory;
  }
  return S_ISLNK(mode) ? FileKind::SymbolicLink : FileKind::Other;
}

class PosixInputFile final : public InputFile {
public:
  PosixInputFile(Descriptor descriptor, std::filesystem::path path,
                 std::uint64_t size)
      : descriptor_(std::move(descriptor)), path_(std::move(path)),
        size_(size) {}

  [[nodiscard]] std::uint64_t size() const override { return size_; }

  std::expected<std::size_t, Error> read(std::span<char> buffer) override {
    while (true) {
      const ssize_t count =
          ::read(descriptor_.get(), buffer.data(), buffer.size());
      if (count >= 0) {
        return static_cast<std::size_t>(count);
      }
      if (errno != EINTR) {
        return std::unexpected(failure("read", path_, errno));
      }
    }
  }

private:
  Descriptor descriptor_;
  std::filesystem::path path_;
  std::uint64_t size_;
};

class PosixOutputFile final : public OutputFile {
public:
  PosixOutputFile(Descriptor descriptor, std::filesystem::path path)
      : descriptor_(std::move(descriptor)), path_(std::move(path)) {}

  std::expected<void, Error> write(std::string_view bytes) override {
    if (const int problem = detail::writeAll(descriptor_.get(), bytes);
        problem != 0) {
      return std::unexpected(failure("write", path_, problem));
    }
    return {};
  }

  std::expected<void, Error> sync() override {
    return done(::fsync(descriptor_.get()), "flush", path_);
  }

private:
  Descriptor descriptor_;
  std::filesystem::path path_;
};

// True when the open descriptor and the path name the same file: nobody has
// removed or replaced the file since it was opened.
bool stillAt(int descriptor, const std::filesystem::path &path) {
  struct stat open{};
  struct stat named{};
  return ::fstat(descriptor, &open) == 0 &&
         ::lstat(path.c_str(), &named) == 0 && open.st_dev == named.st_dev &&
         open.st_ino == named.st_ino;
}

// Holds the lock through its descriptor. Letting go removes the lock file
// first and closes the descriptor after, so that nobody can lock a file that
// is about to disappear and believe to hold the project.
class PosixFileLock final : public FileLock {
public:
  PosixFileLock(Descriptor descriptor, std::filesystem::path path)
      : descriptor_(std::move(descriptor)), path_(std::move(path)) {}
  PosixFileLock(const PosixFileLock &) = delete;
  PosixFileLock &operator=(const PosixFileLock &) = delete;
  PosixFileLock(PosixFileLock &&) = delete;
  PosixFileLock &operator=(PosixFileLock &&) = delete;
  ~PosixFileLock() override {
    if (stillAt(descriptor_.get(), path_)) {
      ::unlink(path_.c_str());
    }
  }

private:
  Descriptor descriptor_;
  std::filesystem::path path_;
};

// What the lock file says, as far as it can be read.
std::string readNote(int descriptor) {
  std::array<char, maxNoteBytes> buffer{};
  const ssize_t count = ::pread(descriptor, buffer.data(), buffer.size(), 0);
  return count > 0 ? std::string{buffer.data(), static_cast<std::size_t>(count)}
                   : std::string{};
}

// Replaces what the lock file says. The note is for people; the lock is held
// whether or not this works, so its failure is not one of taking the lock.
void writeNote(int descriptor, std::string_view note) {
  if (::ftruncate(descriptor, 0) == 0 &&
      detail::writeAll(descriptor, note) == 0) {
    ::fsync(descriptor);
  }
}

} // namespace

namespace detail {

int writeAll(int descriptor, std::string_view bytes) {
  while (!bytes.empty()) {
    const ssize_t written = ::write(descriptor, bytes.data(), bytes.size());
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return errno;
    }
    bytes.remove_prefix(static_cast<std::size_t>(written));
  }
  return 0;
}

LockFailure classifyLockFailure(int errorNumber) {
  switch (errorNumber) {
  case EAGAIN:
  case EACCES:
    return LockFailure::Held;
  case ENOLCK:
  case EOPNOTSUPP:
  case ENOSYS:
  case EINVAL:
    return LockFailure::Unsupported;
  default:
    return LockFailure::Other;
  }
}

bool meansReadOnly(int errorNumber) {
  return errorNumber == EROFS || errorNumber == EACCES || errorNumber == EPERM;
}

std::expected<std::string, Error>
readWholeFile(FileSystem &fileSystem, const std::filesystem::path &file,
              std::uint64_t maxBytes, Links links) {
  const auto tooLarge = [&file, maxBytes] {
    return std::unexpected(Error{
        .code = ErrorCode::TooLarge,
        .where = file.string(),
        .message = "the file is larger than the " + std::to_string(maxBytes) +
                   " bytes that are read",
    });
  };
  auto input = fileSystem.openForReading(file, links);
  if (!input.has_value()) {
    return std::unexpected(std::move(input.error()));
  }
  if ((*input)->size() > maxBytes) {
    return tooLarge();
  }
  std::string content;
  content.reserve((*input)->size());
  std::vector<char> buffer(copyChunkBytes);
  while (true) {
    const auto count = (*input)->read(buffer);
    if (!count.has_value()) {
      return std::unexpected(count.error());
    }
    if (*count == 0) {
      return content;
    }
    // The size was checked at the start; the file may have grown since.
    if (content.size() + *count > maxBytes) {
      return tooLarge();
    }
    content.append(buffer.data(), *count);
  }
}

std::expected<void, Error>
writeNewFile(FileSystem &fileSystem, const std::filesystem::path &file,
             std::string_view bytes, std::optional<std::uint32_t> permissions) {
  auto output = fileSystem.createFile(file, permissions);
  if (!output.has_value()) {
    return std::unexpected(std::move(output.error()));
  }
  if (auto written = (*output)->write(bytes); !written.has_value()) {
    return written;
  }
  return (*output)->sync();
}

std::expected<std::string, Error> hashFile(FileSystem &fileSystem,
                                           const std::filesystem::path &file) {
  auto input = fileSystem.openForReading(file, Links::Refuse);
  if (!input.has_value()) {
    return std::unexpected(std::move(input.error()));
  }
  Sha256 hash;
  std::vector<char> buffer(copyChunkBytes);
  while (true) {
    const auto count = (*input)->read(buffer);
    if (!count.has_value()) {
      return std::unexpected(count.error());
    }
    if (*count == 0) {
      return hash.finishHex();
    }
    hash.update(std::string_view{buffer.data(), *count});
  }
}

} // namespace detail

std::expected<std::unique_ptr<InputFile>, Error>
PosixFileSystem::openForReading(const std::filesystem::path &file,
                                Links links) {
  // Without waiting: a pipe in the file's place must not block the open.
  const int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK |
                    (links == Links::Refuse ? O_NOFOLLOW : 0);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): the POSIX interface
  Descriptor descriptor{::open(file.c_str(), flags)};
  if (!descriptor.open()) {
    return std::unexpected(failure("open", file, errno));
  }
  struct stat status{};
  if (::fstat(descriptor.get(), &status) != 0) {
    return std::unexpected(failure("look at", file, errno));
  }
  if (!S_ISREG(status.st_mode)) {
    return std::unexpected(Error{
        .code = ErrorCode::NotAFile,
        .where = file.string(),
        .message = "cannot open: not a regular file",
    });
  }
  return std::make_unique<PosixInputFile>(
      std::move(descriptor), file, static_cast<std::uint64_t>(status.st_size));
}

std::expected<std::unique_ptr<OutputFile>, Error>
PosixFileSystem::createFile(const std::filesystem::path &file,
                            std::optional<std::uint32_t> permissions) {
  const mode_t mode = permissions.has_value() ? (*permissions & permissionBits)
                                              : defaultFileMode;
  const int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): the POSIX interface
  Descriptor descriptor{::open(file.c_str(), flags, mode)};
  if (!descriptor.open()) {
    return std::unexpected(failure("create", file, errno));
  }
  // The process's mask may have taken bits away that were asked for.
  if (permissions.has_value() && ::fchmod(descriptor.get(), mode) != 0) {
    return std::unexpected(failure("set the permissions of", file, errno));
  }
  return std::make_unique<PosixOutputFile>(std::move(descriptor), file);
}

std::expected<void, Error>
PosixFileSystem::rename(const std::filesystem::path &from,
                        const std::filesystem::path &to) {
  return done(::rename(from.c_str(), to.c_str()), "rename", from);
}

std::expected<void, Error>
PosixFileSystem::removeFile(const std::filesystem::path &file) {
  return removed(::unlink(file.c_str()), "remove", file);
}

std::expected<void, Error>
PosixFileSystem::removeDirectory(const std::filesystem::path &folder) {
  return removed(::rmdir(folder.c_str()), "remove", folder);
}

std::expected<void, Error>
PosixFileSystem::createDirectory(const std::filesystem::path &folder) {
  if (::mkdir(folder.c_str(), folderMode) != 0) {
    const int problem = errno;
    struct stat status{};
    const bool isFolder = problem == EEXIST &&
                          ::stat(folder.c_str(), &status) == 0 &&
                          S_ISDIR(status.st_mode);
    if (!isFolder) {
      return std::unexpected(failure("create", folder, problem));
    }
  }
  return {};
}

std::expected<void, Error>
PosixFileSystem::syncDirectory(const std::filesystem::path &folder) {
  const Descriptor descriptor{
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): the POSIX interface
      ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
  if (!descriptor.open()) {
    return std::unexpected(failure("open", folder, errno));
  }
  return done(::fsync(descriptor.get()), "flush", folder);
}

std::expected<FileInfo, Error>
PosixFileSystem::info(const std::filesystem::path &file) {
  struct stat status{};
  if (::lstat(file.c_str(), &status) != 0) {
    return std::unexpected(failure("look at", file, errno));
  }
  const auto modified = std::chrono::seconds{status.st_mtim.tv_sec} +
                        std::chrono::nanoseconds{status.st_mtim.tv_nsec};
  return FileInfo{
      .kind = kindOf(status.st_mode),
      .size = static_cast<std::uint64_t>(status.st_size),
      .modified = FileTime{modified},
      .permissions = status.st_mode & permissionBits,
  };
}

std::expected<std::vector<std::string>, Error>
PosixFileSystem::list(const std::filesystem::path &folder) {
  std::error_code problem;
  std::filesystem::directory_iterator entry{folder, problem};
  std::vector<std::string> names;
  while (!problem && entry != std::filesystem::directory_iterator{}) {
    names.push_back(entry->path().filename().string());
    entry.increment(problem);
  }
  if (problem) {
    return std::unexpected(failure("list", folder, problem.value()));
  }
  std::ranges::sort(names);
  return names;
}

std::expected<LockAttempt, Error>
PosixFileSystem::lock(const std::filesystem::path &file,
                      std::string_view note) {
  for (int round = 0; round < lockRounds; ++round) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): the POSIX interface
    Descriptor descriptor{::open(file.c_str(),
                                 O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                                 defaultFileMode)};
    if (!descriptor.open()) {
      Error error = failure("open", file, errno);
      if (detail::meansReadOnly(errno)) {
        error.code = ErrorCode::ReadOnly;
      }
      return std::unexpected(std::move(error));
    }
    // A lock on the open file description: it conflicts with every other
    // open of the file, in this process or any other, and goes when the
    // descriptor closes or the process dies.
    struct flock whole{};
    whole.l_type = F_WRLCK;
    whole.l_whence = SEEK_SET;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): the POSIX interface
    const int locked = ::fcntl(descriptor.get(), F_OFD_SETLK, &whole);
    if (locked != 0) {
      const int problem = errno;
      switch (detail::classifyLockFailure(problem)) {
      case detail::LockFailure::Held:
        return LockAttempt{
            .state = LockState::HeldElsewhere,
            .lock = nullptr,
            .note = readNote(descriptor.get()),
        };
      case detail::LockFailure::Unsupported:
        return LockAttempt{
            .state = LockState::Unsupported,
            .lock = nullptr,
            .note = {},
        };
      case detail::LockFailure::Other:
        return std::unexpected(failure("lock", file, problem));
      }
    }
    // The previous holder may have removed the file between the open and the
    // lock; a lock on a file that is gone protects nothing. Start over.
    if (!stillAt(descriptor.get(), file)) {
      continue;
    }
    writeNote(descriptor.get(), note);
    return LockAttempt{
        .state = LockState::Acquired,
        .lock = std::make_unique<PosixFileLock>(std::move(descriptor), file),
        .note = std::string{note},
    };
  }
  return std::unexpected(Error{
      .code = ErrorCode::Io,
      .where = file.string(),
      .message = "the lock file kept being replaced while it was taken",
  });
}

} // namespace lina::format
