// SPDX-License-Identifier: GPL-3.0-or-later
//
// The real file system behind the project folder, on real conditions in the
// scratch folder.
#include "support/faulty_file_system.hpp"
#include "support/scratch.hpp"

#include "internal/file_io.hpp"
#include "lina/format/file_system.hpp"
#include "lina/format/sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cerrno>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lina::format::testing {

namespace {

constexpr std::uint64_t noLimit = ~std::uint64_t{0};

std::uint32_t permissionsOf(const std::filesystem::path &file) {
  struct stat status{};
  REQUIRE(::stat(file.c_str(), &status) == 0);
  return status.st_mode & 07777U;
}

} // namespace

TEST_CASE("format/files: a new file holds exactly what was written") {
  const auto folder = freshScratch("files-write");
  PosixFileSystem files;
  std::string binary;
  for (int index = 0; index < 700'000; ++index) {
    binary += static_cast<char>(index % 251);
  }
  for (const std::string &content :
       {std::string{}, std::string{"text\n"}, std::string(3, '\0'), binary}) {
    const auto file = folder / ("f" + std::to_string(content.size()));
    REQUIRE(detail::writeNewFile(files, file, content).has_value());
    const auto read = detail::readWholeFile(files, file, noLimit);
    REQUIRE(read.has_value());
    REQUIRE(*read == content);
    const auto info = files.info(file);
    REQUIRE(info.has_value());
    REQUIRE(info->kind == FileKind::Regular);
    REQUIRE(info->size == content.size());
    const auto hash = detail::hashFile(files, file);
    REQUIRE(hash.has_value());
    REQUIRE(*hash == sha256Hex(content));
  }
}

TEST_CASE("format/files: a file is written and read in pieces") {
  const auto folder = freshScratch("files-pieces");
  PosixFileSystem files;
  auto output = files.createFile(folder / "a", std::nullopt);
  REQUIRE(output.has_value());
  REQUIRE((*output)->write("one ").has_value());
  REQUIRE((*output)->write("").has_value());
  REQUIRE((*output)->write("two").has_value());
  REQUIRE((*output)->sync().has_value());
  output->reset();

  auto input = files.openForReading(folder / "a", Links::Refuse);
  REQUIRE(input.has_value());
  REQUIRE((*input)->size() == 7);
  std::array<char, 4> buffer{};
  std::string read;
  for (int piece = 0; piece < 3; ++piece) {
    const auto count = (*input)->read(buffer);
    REQUIRE(count.has_value());
    read.append(buffer.data(), *count);
  }
  REQUIRE(read == "one two");
  const auto atEnd = (*input)->read(buffer);
  REQUIRE(atEnd.has_value());
  REQUIRE(*atEnd == 0);

  REQUIRE(
      detail::copyFile(files, folder / "a", folder / "b", 0640U).has_value());
  REQUIRE(readText(folder / "b") == "one two");
  REQUIRE(permissionsOf(folder / "b") == 0640U);
  REQUIRE(detail::copyFile(files, folder / "a", folder / "b", std::nullopt)
              .error()
              .code == ErrorCode::AlreadyExists);
  REQUIRE(detail::copyFile(files, folder / "none", folder / "c", std::nullopt)
              .error()
              .code == ErrorCode::NotFound);
  REQUIRE(!std::filesystem::exists(folder / "c"));
}

TEST_CASE("format/files: a new file must not exist yet") {
  const auto folder = freshScratch("files-exclusive");
  PosixFileSystem files;
  REQUIRE(detail::writeNewFile(files, folder / "a", "first").has_value());
  const auto second = detail::writeNewFile(files, folder / "a", "second");
  REQUIRE(!second.has_value());
  REQUIRE(second.error().code == ErrorCode::AlreadyExists);
  REQUIRE(second.error().where == (folder / "a").string());
  REQUIRE(second.error().message.starts_with("cannot create: "));
  REQUIRE(readText(folder / "a") == "first");

  const auto nowhere =
      detail::writeNewFile(files, folder / "missing" / "a", "x");
  REQUIRE(!nowhere.has_value());
  REQUIRE(nowhere.error().code == ErrorCode::NotFound);

  // A symbolic link in the place counts as something there, whether or not
  // it leads anywhere: nothing is written through it.
  std::filesystem::create_symlink(folder / "a", folder / "link");
  std::filesystem::create_symlink(folder / "nowhere", folder / "dangling");
  REQUIRE(detail::writeNewFile(files, folder / "link", "x").error().code ==
          ErrorCode::AlreadyExists);
  REQUIRE(detail::writeNewFile(files, folder / "dangling", "x").error().code ==
          ErrorCode::AlreadyExists);
  REQUIRE(readText(folder / "a") == "first");
  REQUIRE(!std::filesystem::exists(folder / "nowhere"));
}

TEST_CASE("format/files: a new file gets the permissions asked for") {
  const auto folder = freshScratch("files-permissions");
  PosixFileSystem files;
  const mode_t before = ::umask(0027);
  REQUIRE(detail::writeNewFile(files, folder / "default", "x").has_value());
  // Exactly the bits asked for, whatever the process's mask.
  REQUIRE(
      detail::writeNewFile(files, folder / "private", "x", 0600U).has_value());
  REQUIRE(
      detail::writeNewFile(files, folder / "shared", "x", 0664U).has_value());
  ::umask(before);
  REQUIRE(permissionsOf(folder / "default") == 0640U);
  REQUIRE(permissionsOf(folder / "private") == 0600U);
  REQUIRE(permissionsOf(folder / "shared") == 0664U);
  const auto info = files.info(folder / "shared");
  REQUIRE(info.has_value());
  REQUIRE(info->permissions == 0664U);
}

TEST_CASE("format/files: only regular files are opened for reading") {
  const auto folder = freshScratch("files-read");
  PosixFileSystem files;
  writeText(folder / "file", "content");
  std::filesystem::create_symlink(folder / "file", folder / "link");
  std::filesystem::create_directory(folder / "sub");
  REQUIRE(::mkfifo((folder / "pipe").c_str(), 0600) == 0);

  const auto missing =
      detail::readWholeFile(files, folder / "missing", noLimit);
  REQUIRE(!missing.has_value());
  REQUIRE(missing.error().code == ErrorCode::NotFound);
  REQUIRE(missing.error().message.starts_with("cannot open: "));

  // A folder, and a pipe nobody writes to: refused at once, without waiting.
  for (const char *name : {"sub", "pipe"}) {
    const auto refused =
        detail::readWholeFile(files, folder / name, noLimit, Links::Follow);
    REQUIRE(!refused.has_value());
    REQUIRE(refused.error().code == ErrorCode::NotAFile);
  }
  // A device.
  REQUIRE(detail::readWholeFile(files, "/dev/null", noLimit).error().code ==
          ErrorCode::NotAFile);

  // A symbolic link is followed only on request.
  const auto refused = detail::readWholeFile(files, folder / "link", noLimit);
  REQUIRE(!refused.has_value());
  REQUIRE(refused.error().code == ErrorCode::NotAFile);
  const auto followed =
      detail::readWholeFile(files, folder / "link", noLimit, Links::Follow);
  REQUIRE(followed.has_value());
  REQUIRE(*followed == "content");
  REQUIRE(detail::hashFile(files, folder / "link").error().code ==
          ErrorCode::NotAFile);
}

TEST_CASE("format/files: reading stops at the size limit") {
  const auto folder = freshScratch("files-limit");
  PosixFileSystem files;
  writeText(folder / "ten", "0123456789");
  REQUIRE(detail::readWholeFile(files, folder / "ten", 10).has_value());
  const auto tooLarge = detail::readWholeFile(files, folder / "ten", 9);
  REQUIRE(!tooLarge.has_value());
  REQUIRE(tooLarge.error().code == ErrorCode::TooLarge);
  REQUIRE(tooLarge.error().where == (folder / "ten").string());
  REQUIRE(detail::readWholeFile(files, folder / "ten", 0).error().code ==
          ErrorCode::TooLarge);
}

TEST_CASE("format/files: a file that grows while it is read is still limited") {
  const auto folder = freshScratch("files-grows");
  writeText(folder / "ten", "0123456789");
  FaultyFileSystem files{folder};
  // Someone appends to the file after it was opened and its size was taken.
  files.afterEach([&folder](const std::string &operation) {
    if (operation == "open ten") {
      writeText(folder / "ten", "0123456789 and more");
    }
  });
  const auto grown = detail::readWholeFile(files, folder / "ten", 10);
  REQUIRE(!grown.has_value());
  REQUIRE(grown.error().code == ErrorCode::TooLarge);
  // Within the limit, what is read is what is there by then.
  const auto whole = detail::readWholeFile(files, folder / "ten", 100);
  REQUIRE(whole.has_value());
  REQUIRE(*whole == "0123456789 and more");

  // Reading, hashing and copying report a read that fails.
  files.afterEach({});
  for (std::size_t fault = 0; fault < 2; ++fault) {
    files.failAt(fault, FaultMode::FailOnce);
    REQUIRE(detail::readWholeFile(files, folder / "ten", 100).error().code ==
            ErrorCode::Io);
    files.failAt(fault, FaultMode::FailOnce);
    REQUIRE(detail::hashFile(files, folder / "ten").error().code ==
            ErrorCode::Io);
  }
  for (std::size_t fault = 0; fault < 5; ++fault) {
    CAPTURE(fault);
    std::filesystem::remove(folder / "copy");
    files.failAt(fault, FaultMode::FailOnce);
    REQUIRE(
        !detail::copyFile(files, folder / "ten", folder / "copy", std::nullopt)
             .has_value());
    REQUIRE(files.faultHit());
  }
}

TEST_CASE("format/files: rename replaces the target in one step") {
  const auto folder = freshScratch("files-rename");
  PosixFileSystem files;
  writeText(folder / "old", "old");
  writeText(folder / "new", "new");
  REQUIRE(files.rename(folder / "new", folder / "old").has_value());
  REQUIRE(readText(folder / "old") == "new");
  REQUIRE(filesUnder(folder) == std::vector<std::string>{"old"});

  const auto missing = files.rename(folder / "new", folder / "old");
  REQUIRE(!missing.has_value());
  REQUIRE(missing.error().code == ErrorCode::NotFound);
  REQUIRE(readText(folder / "old") == "new");
}

TEST_CASE("format/files: removing") {
  const auto folder = freshScratch("files-remove");
  PosixFileSystem files;
  writeText(folder / "a", "a");
  REQUIRE(files.removeFile(folder / "a").has_value());
  REQUIRE(!std::filesystem::exists(folder / "a"));
  // What is not there counts as removed.
  REQUIRE(files.removeFile(folder / "a").has_value());
  REQUIRE(files.removeDirectory(folder / "none").has_value());

  REQUIRE(files.createDirectory(folder / "sub").has_value());
  writeText(folder / "sub" / "kept", "kept");
  const auto notAFile = files.removeFile(folder / "sub");
  REQUIRE(!notAFile.has_value());
  REQUIRE(notAFile.error().code == ErrorCode::Io);
  const auto notEmpty = files.removeDirectory(folder / "sub");
  REQUIRE(!notEmpty.has_value());
  REQUIRE(notEmpty.error().code == ErrorCode::Io);
  REQUIRE(readText(folder / "sub" / "kept") == "kept");
  REQUIRE(files.removeFile(folder / "sub" / "kept").has_value());
  REQUIRE(files.removeDirectory(folder / "sub").has_value());
  REQUIRE(!std::filesystem::exists(folder / "sub"));
}

TEST_CASE("format/files: creating a folder") {
  const auto folder = freshScratch("files-mkdir");
  PosixFileSystem files;
  REQUIRE(files.createDirectory(folder / "sub").has_value());
  REQUIRE(std::filesystem::is_directory(folder / "sub"));
  // Again is fine.
  REQUIRE(files.createDirectory(folder / "sub").has_value());

  writeText(folder / "file", "x");
  const auto overFile = files.createDirectory(folder / "file");
  REQUIRE(!overFile.has_value());
  REQUIRE(overFile.error().code == ErrorCode::AlreadyExists);
  const auto orphan = files.createDirectory(folder / "no" / "parent");
  REQUIRE(!orphan.has_value());
  REQUIRE(orphan.error().code == ErrorCode::NotFound);
}

TEST_CASE("format/files: flushing a folder") {
  const auto folder = freshScratch("files-sync");
  PosixFileSystem files;
  REQUIRE(files.syncDirectory(folder).has_value());
  const auto missing = files.syncDirectory(folder / "missing");
  REQUIRE(!missing.has_value());
  REQUIRE(missing.error().code == ErrorCode::NotFound);
  writeText(folder / "file", "x");
  REQUIRE(!files.syncDirectory(folder / "file").has_value());
}

TEST_CASE("format/files: what is at a path") {
  const auto folder = freshScratch("files-info");
  PosixFileSystem files;
  writeText(folder / "early", "12345");
  writeText(folder / "late", "");
  setModified(folder / "early", 1'000);
  setModified(folder / "late", 1'001);
  const auto early = files.info(folder / "early");
  const auto late = files.info(folder / "late");
  REQUIRE(early.has_value());
  REQUIRE(late.has_value());
  REQUIRE(early->size == 5);
  REQUIRE(late->size == 0);
  REQUIRE(late->modified - early->modified == std::chrono::seconds{1});

  const auto missing = files.info(folder / "missing");
  REQUIRE(!missing.has_value());
  REQUIRE(missing.error().code == ErrorCode::NotFound);

  // The kind is that of the thing itself, not of what a link leads to.
  std::filesystem::create_symlink(folder / "early", folder / "link");
  std::filesystem::create_symlink(folder / "nowhere", folder / "dangling");
  REQUIRE(::mkfifo((folder / "pipe").c_str(), 0600) == 0);
  REQUIRE(files.info(folder)->kind == FileKind::Directory);
  REQUIRE(files.info(folder / "link")->kind == FileKind::SymbolicLink);
  REQUIRE(files.info(folder / "dangling")->kind == FileKind::SymbolicLink);
  REQUIRE(files.info(folder / "pipe")->kind == FileKind::Other);
}

TEST_CASE("format/files: listing a folder") {
  const auto folder = freshScratch("files-list");
  PosixFileSystem files;
  const auto empty = files.list(folder);
  REQUIRE(empty.has_value());
  REQUIRE(empty->empty());
  for (const char *name : {"b", "a", "C", ".hidden", "sub"}) {
    writeText(folder / name, "");
  }
  const auto names = files.list(folder);
  REQUIRE(names.has_value());
  REQUIRE(*names == std::vector<std::string>{".hidden", "C", "a", "b", "sub"});

  const auto missing = files.list(folder / "missing");
  REQUIRE(!missing.has_value());
  REQUIRE(missing.error().code == ErrorCode::NotFound);
  const auto notAFolder = files.list(folder / "a");
  REQUIRE(!notAFolder.has_value());
  REQUIRE(notAFolder.error().code == ErrorCode::Io);
}

TEST_CASE("format/files: a write or a flush that fails reports the cause") {
  // A device that is always full: the write itself fails.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): the POSIX interface
  const int full = ::open("/dev/full", O_WRONLY | O_CLOEXEC);
  REQUIRE(full >= 0);
  REQUIRE(detail::writeAll(full, "does not fit") == ENOSPC);
  REQUIRE(::close(full) == 0);

  // A descriptor that is not open for writing.
  const auto folder = freshScratch("files-write-fails");
  writeText(folder / "a", "kept");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): the POSIX interface
  const int readOnly = ::open((folder / "a").c_str(), O_RDONLY | O_CLOEXEC);
  REQUIRE(readOnly >= 0);
  REQUIRE(detail::writeAll(readOnly, "x") == EBADF);
  REQUIRE(detail::writeAll(readOnly, "") == 0);
  REQUIRE(::close(readOnly) == 0);
  REQUIRE(readText(folder / "a") == "kept");
}

TEST_CASE("format/files: error numbers of locking are told apart") {
  using detail::LockFailure;
  REQUIRE(detail::classifyLockFailure(EAGAIN) == LockFailure::Held);
  REQUIRE(detail::classifyLockFailure(EACCES) == LockFailure::Held);
  for (const int unsupported : {ENOLCK, EOPNOTSUPP, ENOSYS, EINVAL}) {
    REQUIRE(detail::classifyLockFailure(unsupported) ==
            LockFailure::Unsupported);
  }
  for (const int other : {EIO, EBADF, EINTR, ENOSPC}) {
    REQUIRE(detail::classifyLockFailure(other) == LockFailure::Other);
  }
  for (const int readOnly : {EROFS, EACCES, EPERM}) {
    REQUIRE(detail::meansReadOnly(readOnly));
  }
  for (const int other : {EIO, ENOENT, ENOSPC, ELOOP}) {
    REQUIRE(!detail::meansReadOnly(other));
  }
}

} // namespace lina::format::testing
