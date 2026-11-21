// SPDX-License-Identifier: GPL-3.0-or-later
//
// The lock of a project folder
// (docs/design/16-project-file-format-encoding.md, "Lock"): held by the
// kernel for as long as the instance has the lock file open.
#include "support/folder_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lina::format::testing {

namespace {

LockNote first() {
  return {
      .processId = 100,
      .hostName = "studio",
      .application = "LINA 0.1.0",
  };
}
// The same process number and host name: another container, say.
LockNote twin() { return first(); }
LockNote second() {
  return {
      .processId = 200,
      .hostName = "laptop",
      .application = "LINA 0.2.0",
  };
}

} // namespace

TEST_CASE("format/lock: the first instance gets the lock and leaves a note") {
  FolderFixture fixture{"lock-acquire"};
  auto lock = fixture.folder.acquireLock(first());
  REQUIRE(lock.has_value());
  REQUIRE(lock->state == LockState::Acquired);
  REQUIRE(lock->held != nullptr);
  REQUIRE(!lock->holder.has_value());
  REQUIRE(readText(fixture.folder.lockPath()) == "100\nstudio\nLINA 0.1.0\n");
  REQUIRE(fixture.files.log() == std::vector<std::string>{"lock .lock"});

  // Letting go removes the lock file.
  lock->held.reset();
  REQUIRE(!std::filesystem::exists(fixture.folder.lockPath()));
}

TEST_CASE("format/lock: a second instance is told who holds it") {
  FolderFixture fixture{"lock-held"};
  auto held = fixture.folder.acquireLock(first());
  REQUIRE(held.has_value());
  REQUIRE(held->state == LockState::Acquired);

  // Through another folder object and another file system object, as another
  // instance would come; the lock conflicts all the same.
  PosixFileSystem otherFiles;
  ProjectFolder other{otherFiles, fixture.root};
  auto refused = other.acquireLock(second());
  REQUIRE(refused.has_value());
  REQUIRE(refused->state == LockState::HeldElsewhere);
  REQUIRE(refused->held == nullptr);
  REQUIRE(refused->holder == first());
  // The holder's note is untouched.
  REQUIRE(readText(fixture.folder.lockPath()) == "100\nstudio\nLINA 0.1.0\n");

  // Process number and host name decide nothing: an instance that happens
  // to have the same ones is refused like any other.
  refused = other.acquireLock(twin());
  REQUIRE(refused.has_value());
  REQUIRE(refused->state == LockState::HeldElsewhere);
  // Nor does asking again from the instance that holds it hand out a second
  // lock.
  refused = fixture.folder.acquireLock(first());
  REQUIRE(refused.has_value());
  REQUIRE(refused->state == LockState::HeldElsewhere);

  // Once the first has closed the project, the second gets it.
  held->held.reset();
  auto taken = other.acquireLock(second());
  REQUIRE(taken.has_value());
  REQUIRE(taken->state == LockState::Acquired);
  REQUIRE(readText(fixture.folder.lockPath()) == "200\nlaptop\nLINA 0.2.0\n");
}

TEST_CASE("format/lock: a lock file nobody holds is no obstacle") {
  FolderFixture fixture{"lock-leftover"};
  // Left behind by an instance that died, whatever it says, even nothing.
  for (const std::string_view leftover :
       {"100\nstudio\nLINA 0.1.0\n", "", "garbage", "\n\n\n"}) {
    CAPTURE(leftover);
    writeText(fixture.folder.lockPath(), leftover);
    auto lock = fixture.folder.acquireLock(second());
    REQUIRE(lock.has_value());
    REQUIRE(lock->state == LockState::Acquired);
    REQUIRE(readText(fixture.folder.lockPath()) == "200\nlaptop\nLINA 0.2.0\n");
  }
  REQUIRE(!std::filesystem::exists(fixture.folder.lockPath()));
}

TEST_CASE("format/lock: an illegible note still says the lock is held") {
  FolderFixture fixture{"lock-note"};
  const auto held = fixture.folder.acquireLock(first());
  REQUIRE(held.has_value());
  PosixFileSystem otherFiles;
  ProjectFolder other{otherFiles, fixture.root};
  // The holder's note as another instance might find it: empty for the
  // moment between taking the lock and writing, or not a note at all.
  for (const std::string_view note : {
           "",
           "100",
           "100\nstudio\n",
           "abc\nstudio\nLINA\n",
           "100\nstudio\nLINA\nmore\n",
           "100x\nstudio\nLINA\n",
       }) {
    CAPTURE(note);
    writeText(fixture.folder.lockPath(), note);
    const auto refused = other.acquireLock(second());
    REQUIRE(refused.has_value());
    REQUIRE(refused->state == LockState::HeldElsewhere);
    REQUIRE(!refused->holder.has_value());
  }
  // A note with empty fields is legible.
  writeText(fixture.folder.lockPath(), "7\n\n\n");
  const auto refused = other.acquireLock(second());
  REQUIRE(refused.has_value());
  REQUIRE(refused->holder ==
          LockNote{.processId = 7, .hostName = "", .application = ""});
}

TEST_CASE("format/lock: the kernel lets go when the holder dies") {
  FolderFixture fixture{"lock-death"};
  std::array<int, 2> toParent{};
  std::array<int, 2> toChild{};
  REQUIRE(::pipe(toParent.data()) == 0);
  REQUIRE(::pipe(toChild.data()) == 0);

  const pid_t child = ::fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    // The other instance: takes the lock, says so, waits to be told to die,
    // and dies without letting go of anything.
    PosixFileSystem files;
    ProjectFolder folder{files, fixture.root};
    auto lock = folder.acquireLock(second());
    char answer =
        lock.has_value() && lock->state == LockState::Acquired ? 'y' : 'n';
    const auto told = ::write(toParent.back(), &answer, 1);
    const auto asked = ::read(toChild.front(), &answer, 1);
    ::_exit(told == 1 && asked == 1 ? 0 : 1);
  }
  char answer = '?';
  REQUIRE(::read(toParent.front(), &answer, 1) == 1);
  REQUIRE(answer == 'y');

  // While it lives, this instance is refused and told who it is.
  auto refused = fixture.folder.acquireLock(first());
  REQUIRE(refused.has_value());
  REQUIRE(refused->state == LockState::HeldElsewhere);
  REQUIRE(refused->holder == second());

  // It dies; its lock file is still there, and is no obstacle.
  REQUIRE(::write(toChild.back(), &answer, 1) == 1);
  int status = 0;
  REQUIRE(::waitpid(child, &status, 0) == child);
  REQUIRE(WIFEXITED(status));
  REQUIRE(WEXITSTATUS(status) == 0);
  REQUIRE(std::filesystem::exists(fixture.folder.lockPath()));
  auto taken = fixture.folder.acquireLock(first());
  REQUIRE(taken.has_value());
  REQUIRE(taken->state == LockState::Acquired);
  REQUIRE(readText(fixture.folder.lockPath()) == "100\nstudio\nLINA 0.1.0\n");
  for (const int descriptor :
       {toParent.front(), toParent.back(), toChild.front(), toChild.back()}) {
    REQUIRE(::close(descriptor) == 0);
  }
}

TEST_CASE("format/lock: a lock file removed by its holder is not locked late") {
  const FolderFixture fixture{"lock-replaced"};
  PosixFileSystem files;
  auto held = files.lock(fixture.folder.lockPath(), "first\n");
  REQUIRE(held.has_value());
  REQUIRE(held->state == LockState::Acquired);
  REQUIRE(held->note == "first\n");
  // The holder lets go: the file goes with the lock, so that whoever comes
  // next locks a file that is really there.
  held->lock.reset();
  REQUIRE(!std::filesystem::exists(fixture.folder.lockPath()));
  auto next = files.lock(fixture.folder.lockPath(), "next\n");
  REQUIRE(next.has_value());
  REQUIRE(next->state == LockState::Acquired);
  REQUIRE(readText(fixture.folder.lockPath()) == "next\n");

  // If someone replaces the lock file behind the holder's back, the holder
  // does not remove the stranger's file when it lets go.
  std::filesystem::remove(fixture.folder.lockPath());
  writeText(fixture.folder.lockPath(), "a stranger's");
  next->lock.reset();
  REQUIRE(readText(fixture.folder.lockPath()) == "a stranger's");
}

TEST_CASE("format/lock: a folder that cannot be written to is told apart") {
  FolderFixture fixture{"lock-read-only"};
  if (::geteuid() == 0) {
    SKIP("permissions do not bind the superuser");
  }
  REQUIRE(::chmod(fixture.root.c_str(), 0555) == 0);
  const auto refused = fixture.folder.acquireLock(first());
  REQUIRE(::chmod(fixture.root.c_str(), 0755) == 0);
  REQUIRE(!refused.has_value());
  REQUIRE(refused.error().code == ErrorCode::ReadOnly);
  REQUIRE(refused.error().where == fixture.folder.lockPath().string());

  // A lock file that is there but may not be written: the same.
  writeText(fixture.folder.lockPath(), "");
  REQUIRE(::chmod(fixture.folder.lockPath().c_str(), 0444) == 0);
  REQUIRE(fixture.folder.acquireLock(first()).error().code ==
          ErrorCode::ReadOnly);
}

TEST_CASE("format/lock: other failures are errors, not answers") {
  FolderFixture fixture{"lock-faults"};
  fixture.files.failAt(0, FaultMode::FailOnce);
  const auto failed = fixture.folder.acquireLock(first());
  REQUIRE(!failed.has_value());
  REQUIRE(failed.error().code == ErrorCode::Io);
  REQUIRE(!std::filesystem::exists(fixture.folder.lockPath()));

  // No folder at all.
  PosixFileSystem files;
  ProjectFolder missing{files, fixture.root / "none"};
  REQUIRE(missing.acquireLock(first()).error().code == ErrorCode::NotFound);
  // A symbolic link in the place of the lock file is not followed.
  std::filesystem::create_symlink(fixture.root / "elsewhere",
                                  fixture.folder.lockPath());
  REQUIRE(fixture.folder.acquireLock(first()).error().code ==
          ErrorCode::NotAFile);
  REQUIRE(!std::filesystem::exists(fixture.root / "elsewhere"));
}

TEST_CASE("format/lock: the note of the running process") {
  const LockNote note = currentProcessNote("LINA tests");
  REQUIRE(note.processId == ::getpid());
  REQUIRE(!note.hostName.empty());
  REQUIRE(note.hostName.find('\n') == std::string::npos);
  REQUIRE(note.application == "LINA tests");

  FolderFixture fixture{"lock-real-note"};
  auto held = fixture.folder.acquireLock(note);
  REQUIRE(held.has_value());
  REQUIRE(held->state == LockState::Acquired);
  PosixFileSystem otherFiles;
  ProjectFolder other{otherFiles, fixture.root};
  const auto refused = other.acquireLock(second());
  REQUIRE(refused.has_value());
  REQUIRE(refused->holder == note);
}

} // namespace lina::format::testing
