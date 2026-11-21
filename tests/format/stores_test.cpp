// SPDX-License-Identifier: GPL-3.0-or-later
//
// The stores for plugin state and media
// (docs/design/16-project-file-format-encoding.md, "Media", "Plugin state").
#include "support/folder_fixture.hpp"

#include "internal/file_io.hpp"
#include "lina/format/sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace lina::format::testing {

namespace {

using Log = std::vector<std::string>;

} // namespace

TEST_CASE("format/state: a state file is named by its content") {
  FolderFixture fixture{"state-store"};
  const std::string content{"opaque\0bytes\xff", 13};
  const auto name = fixture.folder.storeState(content);
  REQUIRE(name.has_value());
  REQUIRE(*name == sha256Hex(content) + ".bin");
  REQUIRE(isStateFileName(*name));
  REQUIRE(readText(fixture.folder.stateFolder() / *name) == content);
  const std::string file = "state/" + *name;
  REQUIRE(fixture.files.log() ==
          Log{"open " + file, "mkdir state", "remove " + file + ".tmp",
              "create " + file + ".tmp", "write " + file + ".tmp",
              "fsync " + file + ".tmp", "rename " + file + ".tmp",
              "sync state"});

  const auto loaded = fixture.folder.loadState(*name);
  REQUIRE(loaded.has_value());
  REQUIRE(*loaded == content);

  // The same content again is found, by reading what is there, not written.
  fixture.files.heal();
  REQUIRE(fixture.folder.storeState(content) == name);
  REQUIRE(fixture.files.log() ==
          Log{"open " + file, "read " + file, "read " + file});

  // Other content gets another name; the empty state is a state too.
  const auto other = fixture.folder.storeState("other");
  const auto empty = fixture.folder.storeState("");
  REQUIRE(other.has_value());
  REQUIRE(empty.has_value());
  REQUIRE(*other != *name);
  REQUIRE(*empty ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
          ".bin");
  REQUIRE(fixture.folder.loadState(*empty)->empty());
  REQUIRE(filesUnder(fixture.folder.stateFolder()).size() == 3);
}

TEST_CASE("format/state: the folder is made when it is not there") {
  FolderFixture fixture{"state-no-folder"};
  std::filesystem::remove_all(fixture.folder.stateFolder());
  const auto name = fixture.folder.storeState("x");
  REQUIRE(name.has_value());
  REQUIRE(fixture.folder.loadState(*name) == std::string{"x"});
}

TEST_CASE("format/state: a file that does not hold its name is not trusted") {
  FolderFixture fixture{"state-corrupt"};
  const auto name = fixture.folder.storeState("the real content");
  REQUIRE(name.has_value());
  const auto file = fixture.folder.stateFolder() / *name;

  // Same size, other bytes; cut short; empty: found by hashing on load, and
  // put right by storing the content again.
  for (const std::string_view damaged : {"the fake content", "the real", ""}) {
    CAPTURE(damaged);
    writeText(file, damaged);
    const auto corrupt = fixture.folder.loadState(*name);
    REQUIRE(!corrupt.has_value());
    REQUIRE(corrupt.error().code == ErrorCode::Corrupt);
    REQUIRE(corrupt.error().where == file.string());
    REQUIRE(fixture.folder.storeState("the real content") == name);
    REQUIRE(fixture.folder.loadState(*name) == std::string{"the real content"});
  }
  // A symbolic link in the place of a state file is not read through.
  std::filesystem::remove(file);
  writeText(fixture.root / "elsewhere", "the real content");
  std::filesystem::create_symlink(fixture.root / "elsewhere", file);
  REQUIRE(fixture.folder.loadState(*name).error().code == ErrorCode::NotAFile);
  REQUIRE(fixture.folder.storeState("the real content").error().code ==
          ErrorCode::NotAFile);
}

TEST_CASE("format/state: only names of state files are looked up") {
  FolderFixture fixture{"state-names"};
  writeText(fixture.root / "project.lina", "secret");
  for (const std::string &name : {
           std::string{"../project.lina"},
           std::string{"project.lina"},
           std::string{},
           std::string{".."},
           std::string{".bin"},
           std::string(64, 'a'),
           std::string(64, 'A') + ".bin",
           std::string(63, 'a') + ".bin",
           std::string(65, 'a') + ".bin",
           std::string(64, 'a') + ".BIN",
           "../" + std::string(61, 'a') + ".bin",
       }) {
    CAPTURE(name);
    REQUIRE(!isStateFileName(name));
    const auto loaded = fixture.folder.loadState(name);
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == ErrorCode::Value);
  }
  const std::string good = std::string(64, 'a') + ".bin";
  REQUIRE(isStateFileName(good));
  REQUIRE(isStateFileName("0123456789abcdef0123456789abcdef0123456789abcdef"
                          "0123456789abcdef.bin"));
  REQUIRE(fixture.folder.loadState(good).error().code == ErrorCode::NotFound);
  REQUIRE(maxStateBytes == 1'073'741'824U);
}

TEST_CASE("format/state: a store that fails leaves no broken file") {
  for (const FaultMode mode : {FaultMode::FailOnce, FaultMode::Crash}) {
    for (std::size_t fault = 0; fault < 8; ++fault) {
      CAPTURE(fault);
      FolderFixture fixture{"state-faults"};
      fixture.files.failAt(fault, mode);
      const auto name = fixture.folder.storeState("content");
      REQUIRE(fixture.files.faultHit());
      REQUIRE(!name.has_value());
      fixture.files.heal();
      if (mode == FaultMode::FailOnce) {
        REQUIRE(temporaryFilesUnder(fixture.root).empty());
      }
      // Whatever is in state/ under the name is the whole content.
      const std::string expected = sha256Hex("content") + ".bin";
      if (std::filesystem::exists(fixture.folder.stateFolder() / expected)) {
        REQUIRE(fixture.folder.loadState(expected) == std::string{"content"});
      }
      REQUIRE(fixture.folder.removeStaleTemporaries().has_value());
      REQUIRE(fixture.folder.storeState("content").has_value());
      REQUIRE(filesUnder(fixture.folder.stateFolder()) ==
              std::vector<std::string>{expected});
    }
  }
  FolderFixture fixture{"state-faults"};
  REQUIRE(fixture.folder.storeState("content").has_value());
  // Looking at what is stored already can fail too.
  for (std::size_t fault = 0; fault < 3; ++fault) {
    fixture.files.failAt(fault, FaultMode::FailOnce);
    REQUIRE(!fixture.folder.storeState("content").has_value());
    fixture.files.failAt(fault, FaultMode::FailOnce);
    REQUIRE(
        !fixture.folder.loadState(sha256Hex("content") + ".bin").has_value());
  }
}

TEST_CASE("format/media: a file's name in media/ carries its content") {
  REQUIRE(mediaFileName("kick.wav", "3f9a1c2e5d0b") == "kick-3f9a1c2e.wav");
  REQUIRE(mediaFileName("/samples/drums/kick.wav", "3f9a1c2e5d0b") ==
          "kick-3f9a1c2e.wav");
  REQUIRE(mediaFileName("take.1.flac", "00000000ffff") ==
          "take.1-00000000.flac");
  REQUIRE(mediaFileName("noextension", "abcdef0123") == "noextension-abcdef01");
  REQUIRE(mediaFileName(".hidden", "abcdef0123") == ".hidden-abcdef01");
}

TEST_CASE("format/media: importing copies the file in and describes it") {
  FolderFixture fixture{"media-import"};
  const auto outside = fixture.root.parent_path() / "outside";
  std::filesystem::create_directories(outside);
  // Longer than one piece of the copy, so that the pieces are put together.
  std::string audio = "RIFF....WAVEfmt ";
  while (audio.size() < (3 * detail::copyChunkBytes) + 17) {
    audio += static_cast<char>(audio.size() % 253);
  }
  writeText(outside / "kick.wav", audio);

  const auto imported = fixture.folder.importMedia(outside / "kick.wav");
  REQUIRE(imported.has_value());
  const std::string hash = sha256Hex(audio);
  REQUIRE(imported->contentHash == hash);
  REQUIRE(imported->sizeBytes == audio.size());
  REQUIRE(imported->path == "media/kick-" + hash.substr(0, 8) + ".wav");
  REQUIRE(readText(fixture.root / imported->path) == audio);
  // The source is untouched and nothing temporary is left.
  REQUIRE(readText(outside / "kick.wav") == audio);
  REQUIRE(temporaryFilesUnder(fixture.root).empty());
  // Read in pieces and written in pieces, never as one block.
  REQUIRE(std::ranges::count(fixture.files.log(),
                             "write media/kick.wav.import.tmp") == 4);

  // What it returns is what a media reference may hold.
  const auto location = fixture.folder.mediaLocation(imported->path);
  REQUIRE(location.has_value());
  REQUIRE(location.value_or("") == fixture.root / imported->path);

  // The source may be a symbolic link; a folder or nothing may not.
  std::filesystem::create_symlink(outside / "kick.wav", outside / "link.wav");
  const auto throughLink = fixture.folder.importMedia(outside / "link.wav");
  REQUIRE(throughLink.has_value());
  REQUIRE(throughLink->path == imported->path);
  REQUIRE(fixture.folder.importMedia(outside).error().code ==
          ErrorCode::NotAFile);
  REQUIRE(fixture.folder.importMedia(outside / "missing.wav").error().code ==
          ErrorCode::NotFound);
  REQUIRE(filesUnder(fixture.folder.mediaFolder()).size() == 1);
}

TEST_CASE("format/media: the same content is stored once") {
  FolderFixture fixture{"media-once"};
  const auto outside = fixture.root.parent_path() / "outside";
  std::filesystem::create_directories(outside / "copy");
  writeText(outside / "kick.wav", "kick");
  writeText(outside / "copy" / "kick.wav", "kick");
  writeText(outside / "renamed.aiff", "kick");
  writeText(outside / "snare.wav", "snare");

  const auto first = fixture.folder.importMedia(outside / "kick.wav");
  REQUIRE(first.has_value());
  // Again, from another folder, and under another name: the stored file is
  // found each time.
  for (const auto &source : {
           outside / "kick.wav",
           outside / "copy" / "kick.wav",
           outside / "renamed.aiff",
       }) {
    const auto again = fixture.folder.importMedia(source);
    REQUIRE(again.has_value());
    REQUIRE(again->path == first->path);
    REQUIRE(again->contentHash == first->contentHash);
    REQUIRE(again->sizeBytes == 4);
  }
  REQUIRE(filesUnder(fixture.folder.mediaFolder()).size() == 1);

  // Other content is another file, even under the same name.
  writeText(outside / "copy" / "kick.wav", "another kick");
  const auto other = fixture.folder.importMedia(outside / "copy" / "kick.wav");
  const auto snare = fixture.folder.importMedia(outside / "snare.wav");
  REQUIRE(other.has_value());
  REQUIRE(snare.has_value());
  REQUIRE(other->path != first->path);
  REQUIRE(filesUnder(fixture.folder.mediaFolder()).size() == 3);
}

TEST_CASE("format/media: a stored file is never overwritten by other content") {
  FolderFixture fixture{"media-collision"};
  const auto outside = fixture.root.parent_path() / "outside";
  std::filesystem::create_directories(outside);
  writeText(outside / "kick.wav", "kick");
  const std::string name = mediaFileName("kick.wav", sha256Hex("kick"));
  // The name is taken by a file with other content: it carries the same
  // eight digits, which two contents share once in four thousand million.
  writeText(fixture.folder.mediaFolder() / name, "not the kick");
  // A file that merely looks similar is not a candidate.
  writeText(fixture.folder.mediaFolder() / ("kick" + name.substr(5)), "kick");

  const auto imported = fixture.folder.importMedia(outside / "kick.wav");
  REQUIRE(!imported.has_value());
  REQUIRE(imported.error().code == ErrorCode::AlreadyExists);
  REQUIRE(imported.error().where ==
          (fixture.folder.mediaFolder() / name).string());
  REQUIRE(readText(fixture.folder.mediaFolder() / name) == "not the kick");
  REQUIRE(temporaryFilesUnder(fixture.root).empty());
}

TEST_CASE("format/media: an import that fails is reported and leaves no half") {
  const auto outside =
      std::filesystem::path{freshScratch("media-faults-source")};
  writeText(outside / "kick.wav", "kick");
  writeText(outside / "other.wav", "kick");
  std::size_t steps = 0;
  {
    FolderFixture fixture{"media-faults"};
    REQUIRE(fixture.folder.importMedia(outside / "kick.wav").has_value());
    steps = fixture.files.log().size();
    REQUIRE(fixture.files.log().back() == "sync media");
  }
  for (const FaultMode mode : {FaultMode::FailOnce, FaultMode::Crash}) {
    for (std::size_t fault = 0; fault < steps; ++fault) {
      CAPTURE(fault);
      FolderFixture fixture{"media-faults"};
      fixture.files.failAt(fault, mode);
      const auto imported = fixture.folder.importMedia(outside / "kick.wav");
      REQUIRE(fixture.files.faultHit());
      REQUIRE(!imported.has_value());
      fixture.files.heal();
      if (mode == FaultMode::FailOnce) {
        REQUIRE(temporaryFilesUnder(fixture.root).empty());
      }
      REQUIRE(fixture.folder.removeStaleTemporaries().has_value());
      // Whatever has a final name in media/ is the whole file.
      for (const std::string &file : filesUnder(fixture.folder.mediaFolder())) {
        REQUIRE(readText(fixture.folder.mediaFolder() / file) == "kick");
      }
      REQUIRE(fixture.folder.importMedia(outside / "kick.wav").has_value());
      REQUIRE(filesUnder(fixture.folder.mediaFolder()).size() == 1);
    }
  }
  // Finding content that is stored already has steps of its own.
  FolderFixture fixture{"media-faults"};
  REQUIRE(fixture.folder.importMedia(outside / "kick.wav").has_value());
  fixture.files.heal();
  REQUIRE(fixture.folder.importMedia(outside / "other.wav").has_value());
  const std::size_t again = fixture.files.log().size();
  for (std::size_t fault = 0; fault < again; ++fault) {
    CAPTURE(fault);
    fixture.files.failAt(fault, FaultMode::FailOnce);
    REQUIRE(!fixture.folder.importMedia(outside / "other.wav").has_value());
    REQUIRE(fixture.files.faultHit());
    fixture.files.heal();
    REQUIRE(temporaryFilesUnder(fixture.root).empty());
    REQUIRE(filesUnder(fixture.folder.mediaFolder()).size() == 1);
  }

  // Names a document could not hold are refused before anything is read.
  fixture.files.heal();
  for (const std::filesystem::path &source : {
           outside / "caf\xE9.wav",
           std::filesystem::path{"/"},
           std::filesystem::path{},
       }) {
    const auto refused = fixture.folder.importMedia(source);
    REQUIRE(!refused.has_value());
    REQUIRE(refused.error().code == ErrorCode::Value);
  }
  REQUIRE(fixture.files.log().empty());
}

TEST_CASE("format/media: references resolve inside the folder or not at all") {
  PosixFileSystem files;
  const ProjectFolder folder{files, "/music/My Song"};
  REQUIRE(folder.mediaLocation("media/kick-3f9a1c2e.wav") ==
          std::filesystem::path{"/music/My Song/media/kick-3f9a1c2e.wav"});
  REQUIRE(folder.mediaLocation("loose.wav") ==
          std::filesystem::path{"/music/My Song/loose.wav"});
  // A file referenced where it is.
  REQUIRE(folder.mediaLocation("/samples/snare.flac") ==
          std::filesystem::path{"/samples/snare.flac"});
  // Nothing relative leads out of the folder, or wanders inside it.
  for (const std::string_view path : {
           "../other/kick.wav",
           "media/../../kick.wav",
           "",
           "./kick.wav",
           "media//kick.wav",
           "media/./kick.wav",
           "media/",
           "media/..",
       }) {
    CAPTURE(path);
    REQUIRE(!folder.mediaLocation(path).has_value());
  }
}

} // namespace lina::format::testing
