// SPDX-License-Identifier: GPL-3.0-or-later
//
// Safety of saving (docs/design/10 "Safety of saving", 16 "Saving"): a save
// that fails or dies at any step leaves a project that loads and the backup
// that was there; and state files go only when the owner says what to keep.
#include "support/documents.hpp"
#include "support/folder_fixture.hpp"
#include "support/text_edit.hpp"

#include "internal/save.hpp"
#include "lina/format/sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace lina::format::testing {

namespace {

constexpr std::string_view otherStateContent = "the state of another document";

// The large document with its first scene renamed: three of these are three
// documents that are told apart, each referring to a state file and a media
// file inside the folder.
model::Document named(std::string sceneName) {
  model::Document document = everythingDocument();
  mustApply(document, model::RenameScene{
                          .scene = document.project().scenes.front().id,
                          .name = std::move(sceneName),
                      });
  return document;
}

// The same with another state for the instrument.
model::Document withOtherState(std::string sceneName) {
  model::Document document = named(std::move(sceneName));
  mustApply(
      document,
      model::SetDeviceProperty{
          .device = document.project().tracks.at(1).devices.front().id,
          .property = model::StateValue{sha256Hex(otherStateContent) + ".bin"},
      });
  return document;
}

std::string backupText(const ProjectFolder &folder) {
  return readText(folder.backupFolder() / "project.lina");
}

// A folder with a history: "oldest" is the backup, "current" is saved, and
// "newest" is in the recovery copy, about to be saved.
struct History {
  DocumentImage oldest{imageOf(named("oldest"))};
  DocumentImage current{imageOf(named("current"))};
  DocumentImage newest{imageOf(named("newest"))};

  void prepare(FolderFixture &fixture) const {
    REQUIRE(fixture.created);
    provideReferenced(fixture.folder, oldest);
    REQUIRE(fixture.folder.save(oldest).has_value());
    REQUIRE(fixture.folder.save(current).has_value());
    REQUIRE(fixture.folder.writeRecovery(newest).has_value());
    REQUIRE(backupText(fixture.folder) == oldest.bytes());
    fixture.files.heal();
  }
};

// Saves the newest document with a fault on every operation in turn.
void checkEveryFault(FaultMode mode, std::string_view scratchName) {
  const History history;

  // What a save does when nothing fails, to know where the swap is.
  std::vector<std::string> clean;
  {
    FolderFixture fixture{scratchName};
    history.prepare(fixture);
    REQUIRE(fixture.folder.save(history.newest).has_value());
    clean = fixture.files.log();
  }
  const auto swap = static_cast<std::size_t>(
      std::ranges::find(clean, "rename project.lina.tmp") - clean.begin());
  const auto rotation = static_cast<std::size_t>(
      std::ranges::find(clean, "rename backup/project.lina.tmp") -
      clean.begin());
  REQUIRE(clean.at(swap + 1) == "sync .");
  REQUIRE(rotation == swap + 2);
  REQUIRE(rotation + 1 < clean.size());

  for (std::size_t fault = 0; fault < clean.size(); ++fault) {
    CAPTURE(fault, clean.at(fault));
    FolderFixture fixture{scratchName};
    history.prepare(fixture);
    fixture.files.failAt(fault, mode);
    const auto report = fixture.folder.save(history.newest);
    REQUIRE(fixture.files.faultHit());
    fixture.files.heal();

    // Whatever happened, project.lina is whole and is one of the two.
    const std::string onDisk = readText(fixture.folder.documentPath());
    REQUIRE(fixture.folder.load().has_value());
    // And the backup is whole: a document that was saved before, never a
    // copy of what project.lina holds now.
    const std::string backup = backupText(fixture.folder);
    REQUIRE(readDocument(backup).has_value());
    REQUIRE(backup != onDisk);

    if (fault <= swap) {
      // Up to and including the swap: the save failed, and both the project
      // and its backup are exactly what they were.
      REQUIRE(!report.has_value());
      REQUIRE(onDisk == history.current.bytes());
      REQUIRE(backup == history.oldest.bytes());
      // The recovery copy with the unsaved work is still there.
      REQUIRE(fixture.folder.findRecovery()->has_value());
    } else {
      REQUIRE(onDisk == history.newest.bytes());
      // The flush of the folder is the last step that can fail the save.
      REQUIRE(report.has_value() == (fault > swap + 1));
      if (report.has_value()) {
        REQUIRE(!report->afterSwapProblems.empty());
      }
      // Until the backup has taken its name, the older backup stands.
      if (fault <= rotation) {
        REQUIRE(backup == history.oldest.bytes());
        REQUIRE((!report.has_value() || report->backupName.empty()));
      } else {
        REQUIRE(backup == history.current.bytes());
        REQUIRE(report->backupName == "project.lina");
      }
    }
    if (mode == FaultMode::FailOnce && fault != swap + 1) {
      // A save that returns has tidied up after itself; only when the flush
      // of the folder failed is the prepared backup left for the next save.
      REQUIRE(temporaryFilesUnder(fixture.root).empty());
    }

    // The next save, with the fault gone, puts everything right.
    REQUIRE(fixture.folder.removeStaleTemporaries().has_value());
    const auto retried = fixture.folder.save(history.newest);
    REQUIRE(retried.has_value());
    REQUIRE(retried->afterSwapProblems.empty());
    REQUIRE(retried->backupName == "project.lina");
    REQUIRE(temporaryFilesUnder(fixture.root).empty());
    REQUIRE(readText(fixture.folder.documentPath()) == history.newest.bytes());
    REQUIRE(backupText(fixture.folder) == onDisk);
    REQUIRE(!std::filesystem::exists(fixture.folder.recoveryFolder()));
  }
}

} // namespace

TEST_CASE("format/save: a save that fails at any step keeps project and "
          "backup") {
  checkEveryFault(FaultMode::FailOnce, "save-fail-once");
}

TEST_CASE("format/save: a save that dies at any step keeps project and "
          "backup") {
  checkEveryFault(FaultMode::Crash, "save-crash");
}

TEST_CASE("format/save: a first save that fails leaves no project behind") {
  const DocumentImage image = imageOf(minimalDocument());
  for (const FaultMode mode : {FaultMode::FailOnce, FaultMode::Crash}) {
    for (std::size_t fault = 0; fault < 6; ++fault) {
      CAPTURE(fault);
      FolderFixture fixture{"save-first-faults"};
      fixture.files.failAt(fault, mode);
      const auto report = fixture.folder.save(image);
      REQUIRE(!report.has_value());
      REQUIRE(report.error().message == "injected failure");
      fixture.files.heal();
      // Still no project, rather than half of one.
      REQUIRE(fixture.folder.load().error().code == ErrorCode::NotFound);
      if (mode == FaultMode::FailOnce) {
        REQUIRE(filesUnder(fixture.root).empty());
      }
      REQUIRE(fixture.folder.save(image).has_value());
      REQUIRE(filesUnder(fixture.root) ==
              std::vector<std::string>{"project.lina"});
    }
  }
}

TEST_CASE("format/save: a save goes through with files missing, and says so") {
  FolderFixture fixture{"save-referenced"};
  const History history;
  history.prepare(fixture);
  const std::string kick = "media/kick-3f9a1c2e.wav";

  // Nothing missing: nothing listed.
  auto report = fixture.folder.save(history.newest);
  REQUIRE(report.has_value());
  REQUIRE(report->missingStates.empty());
  REQUIRE(report->missingMedia.empty());

  // A state file that was never stored, and a media file that is gone: the
  // save succeeds, and the report names both.
  const model::Document document = withOtherState("needs more");
  const DocumentImage needsMore = imageOf(document);
  const std::string state = needsMore.stateRefs().front();
  std::filesystem::remove(fixture.root / kick);
  fixture.files.heal();
  report = fixture.folder.save(needsMore);
  REQUIRE(report.has_value());
  REQUIRE(report->missingStates == std::vector<std::string>{state});
  REQUIRE(report->missingMedia == std::vector<std::string>{kick});
  REQUIRE(report->backupName == "project.lina");
  REQUIRE(report->afterSwapProblems.empty());
  // They were looked for before anything was written.
  REQUIRE(fixture.files.log().at(0) == "info state/" + state);
  REQUIRE(fixture.files.log().at(1) == "info " + kick);
  // The document on disk still refers to them, and loads.
  REQUIRE(readText(fixture.folder.documentPath()) == needsMore.bytes());
  const auto loaded = fixture.folder.load();
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->document.project() == document.project());
  const auto again = writeDocument(loaded->document, testWriter);
  REQUIRE(again.has_value());
  REQUIRE(again->stateRefs() == std::vector<std::string>{state});
  REQUIRE(again->mediaPaths() == std::vector<std::string>{kick});
  REQUIRE(backupText(fixture.folder) == history.newest.bytes());

  // A folder in the place of a file is not the file.
  std::filesystem::create_directory(fixture.root / kick);
  std::filesystem::create_directory(fixture.folder.stateFolder() / state);
  report = fixture.folder.save(needsMore);
  REQUIRE(report.has_value());
  REQUIRE(report->missingStates == std::vector<std::string>{state});
  REQUIRE(report->missingMedia == std::vector<std::string>{kick});
  std::filesystem::remove(fixture.root / kick);
  std::filesystem::remove(fixture.folder.stateFolder() / state);

  // Once they are back, nothing is listed. The media file referenced outside
  // the project is not looked for at all.
  REQUIRE(fixture.folder.storeState(otherStateContent) == state);
  writeText(fixture.root / kick, "audio");
  report = fixture.folder.save(needsMore);
  REQUIRE(report.has_value());
  REQUIRE(report->missingStates.empty());
  REQUIRE(report->missingMedia.empty());

  // Not being able to look is an error, and then nothing is changed.
  for (std::size_t fault = 0; fault < 2; ++fault) {
    fixture.files.failAt(fault, FaultMode::FailOnce);
    const auto failed = fixture.folder.save(history.newest);
    REQUIRE(!failed.has_value());
    REQUIRE(failed.error().code == ErrorCode::Io);
    REQUIRE(readText(fixture.folder.documentPath()) == needsMore.bytes());
  }
}

TEST_CASE("format/save: something in the place of the temporary file") {
  FolderFixture fixture{"save-in-the-way"};
  const DocumentImage first = imageOf(minimalDocument());
  REQUIRE(fixture.folder.save(first).has_value());
  const auto temporary = fixture.root / "project.lina.tmp";

  // A file left by a crash is never loaded, and is replaced.
  writeText(temporary, R"({"format": 1, "torn)");
  REQUIRE(fixture.folder.load().has_value());
  REQUIRE(fixture.folder.save(first).has_value());
  REQUIRE(!std::filesystem::exists(temporary));

  // An empty folder of that name is no obstacle either.
  std::filesystem::create_directory(temporary);
  REQUIRE(fixture.folder.save(first).has_value());
  REQUIRE(!std::filesystem::exists(temporary));

  // A folder with something in it is not LINA's to remove: the save fails
  // and says what is in the way.
  std::filesystem::create_directory(temporary);
  writeText(temporary / "mine.txt", "mine");
  const auto report = fixture.folder.save(first);
  REQUIRE(!report.has_value());
  REQUIRE(report.error().code == ErrorCode::NotAFile);
  REQUIRE(report.error().where == temporary.string());
  REQUIRE(report.error().message.find("is in the place of a temporary file") !=
          std::string::npos);
  REQUIRE(readText(temporary / "mine.txt") == "mine");
  REQUIRE(fixture.folder.load().has_value());
  // The prepared backup did not stay behind.
  REQUIRE(temporaryFilesUnder(fixture.folder.backupFolder()).empty());
}

TEST_CASE("format/save: a document that cannot be read is kept apart") {
  FolderFixture fixture{"save-unreadable"};
  const DocumentImage good = imageOf(minimalDocument());
  const DocumentImage next = imageOf(named("next"));
  provideReferenced(fixture.folder, next);
  REQUIRE(fixture.folder.save(good).has_value());
  REQUIRE(fixture.folder.save(good).has_value());
  REQUIRE(backupText(fixture.folder) == good.bytes());

  // Something overwrote project.lina with what is no project.
  for (const std::string_view damaged : {
           "not a project at all",
           R"({"format": "1"})",
           "{\"format\": -1}",
           "{\"format\": 4294967296}",
           "[1]",
           "{\"format\": [1]}",
           "",
           "\xEF\xBB\xBF{\"format\": 1}",
       }) {
    CAPTURE(damaged);
    writeText(fixture.folder.documentPath(), damaged);
    // It does not stop the user from saving, it is not thrown away, and it
    // does not push out the backup that is good.
    const auto report = fixture.folder.save(next);
    REQUIRE(report.has_value());
    REQUIRE(report->backupName == "project.unreadable.lina");
    REQUIRE(report->afterSwapProblems.empty());
    REQUIRE(readText(fixture.folder.backupFolder() /
                     "project.unreadable.lina") == damaged);
    REQUIRE(backupText(fixture.folder) == good.bytes());
    REQUIRE(readText(fixture.folder.documentPath()) == next.bytes());
  }

  // A document whose version can be read, but which is damaged further on,
  // counts as a document of that version: only the version is looked at.
  const std::string torn = good.bytes().substr(0, good.bytes().size() / 2);
  writeText(fixture.folder.documentPath(), torn);
  const auto report = fixture.folder.save(next);
  REQUIRE(report.has_value());
  REQUIRE(report->backupName == "project.lina");
  REQUIRE(backupText(fixture.folder) == torn);
}

TEST_CASE("format/save: a document past the size limit is kept apart too") {
  FolderFixture fixture{"save-oversized"};
  const DocumentImage good = imageOf(minimalDocument());
  REQUIRE(fixture.folder.save(good).has_value());
  REQUIRE(fixture.folder.save(good).has_value());
  // As if the limit were small: the document on disk is beyond it.
  const detail::ReadingRules rules{
      .history = detail::formatHistory(),
      .maxDocumentBytes = 100,
  };
  fixture.files.heal();
  auto report = detail::saveProject(fixture.files, fixture.folder, good, rules);
  REQUIRE(report.has_value());
  REQUIRE(report->backupName == "project.unreadable.lina");
  REQUIRE(readText(fixture.folder.backupFolder() / "project.unreadable.lina") ==
          good.bytes());
  REQUIRE(backupText(fixture.folder) == good.bytes());
  REQUIRE(temporaryFilesUnder(fixture.root).empty());

  // Copying it in pieces can fail at every step, and then nothing changed.
  const std::size_t swap = static_cast<std::size_t>(
      std::ranges::find(fixture.files.log(), "rename project.lina.tmp") -
      fixture.files.log().begin());
  const DocumentImage next = imageOf(named("next"));
  provideReferenced(fixture.folder, next);
  std::filesystem::remove(fixture.folder.backupFolder() /
                          "project.unreadable.lina");
  for (std::size_t fault = 0; fault < swap; ++fault) {
    CAPTURE(fault);
    fixture.files.failAt(fault + next.stateRefs().size() +
                             next.mediaPaths().size(),
                         FaultMode::FailOnce);
    report = detail::saveProject(fixture.files, fixture.folder, next, rules);
    REQUIRE(fixture.files.faultHit());
    REQUIRE(!report.has_value());
    fixture.files.heal();
    REQUIRE(readText(fixture.folder.documentPath()) == good.bytes());
    REQUIRE(filesUnder(fixture.folder.backupFolder()) ==
            std::vector<std::string>{"project.lina"});
    REQUIRE(temporaryFilesUnder(fixture.root).empty());
  }
}

TEST_CASE("format/save: something foreign in recovery/ does not fail a save") {
  FolderFixture fixture{"save-recovery-foreign"};
  const History history;
  history.prepare(fixture);
  writeText(fixture.folder.recoveryFolder() / "notes.txt", "mine");
  const auto report = fixture.folder.save(history.newest);
  REQUIRE(report.has_value());
  REQUIRE(report->backupName == "project.lina");
  REQUIRE(report->afterSwapProblems.size() == 1);
  REQUIRE(report->afterSwapProblems.front().where ==
          fixture.folder.recoveryFolder().string());
  // The copy itself is gone, so nothing stale is offered.
  REQUIRE(!std::filesystem::exists(fixture.folder.recoveryDocumentPath()));
  REQUIRE(!fixture.folder.findRecovery()->has_value());
  REQUIRE(readText(fixture.folder.documentPath()) == history.newest.bytes());
}

TEST_CASE("format/save: a document of an older format is kept by its version") {
  // A made-up past: version 0 called the tempo "bpm". Its one step leads to
  // the real version 1.
  const std::array<detail::Migration, 1> steps{
      [](detail::JsonValue &root,
         detail::MigrationContext & /*context*/) -> std::optional<Error> {
        for (detail::JsonMember &member :
             std::get<detail::JsonValue::Object>(root.data)) {
          if (member.key == "bpm") {
            member.key = "tempo";
          }
        }
        return std::nullopt;
      },
  };
  const detail::ReadingRules rules{
      .history = {.oldest = 0, .migrations = steps},
      .maxDocumentBytes = maxDocumentBytes,
  };
  REQUIRE(rules.history.current() == formatVersion);

  FolderFixture fixture{"save-older-format"};
  const DocumentImage original = imageOf(named("original"));
  const DocumentImage edited = imageOf(withOtherState("edited"));
  provideReferenced(fixture.folder, original);
  REQUIRE(fixture.folder.storeState(otherStateContent).has_value());
  std::string old = original.bytes();
  old = replaced(old, "\"format\": 1", "\"format\": 0");
  old = replaced(old, "\"tempo\": 93.47", "\"bpm\": 93.47");
  writeText(fixture.folder.documentPath(), old);

  // This build's reader refuses it; the reader with the past takes it.
  REQUIRE(fixture.folder.load().error().code == ErrorCode::Version);
  const auto migrated = detail::readDocumentWith(old, rules.history);
  REQUIRE(migrated.has_value());
  REQUIRE(migrated->formatRead == 0);
  REQUIRE(migrated->document.project() == named("original").project());

  // The first save keeps the original beside the new document, named by its
  // version.
  auto report =
      detail::saveProject(fixture.files, fixture.folder, edited, rules);
  REQUIRE(report.has_value());
  REQUIRE(report->backupName == "project.v0.lina");
  REQUIRE(report->afterSwapProblems.empty());
  REQUIRE(readText(fixture.folder.backupFolder() / "project.v0.lina") == old);
  REQUIRE(readText(fixture.folder.documentPath()) == edited.bytes());

  // Later saves rotate the ordinary backup and leave the original alone.
  for (int round = 0; round < 2; ++round) {
    report = detail::saveProject(fixture.files, fixture.folder, edited, rules);
    REQUIRE(report.has_value());
    REQUIRE(report->backupName == "project.lina");
  }
  REQUIRE(readText(fixture.folder.backupFolder() / "project.v0.lina") == old);
  REQUIRE(filesUnder(fixture.folder.backupFolder()) ==
          std::vector<std::string>{"project.lina", "project.v0.lina"});

  // The state file that only the original refers to is kept for it.
  const auto removed =
      detail::collectStates(fixture.files, fixture.folder, {}, rules);
  REQUIRE(removed.has_value());
  REQUIRE(removed->empty());
  REQUIRE(std::filesystem::exists(fixture.folder.stateFolder() /
                                  original.stateRefs().front()));
  // The real reader cannot read the original, so with it nothing may go.
  const std::vector<std::string> nothing;
  const auto refused = fixture.folder.collectStates(nothing);
  REQUIRE(!refused.has_value());
  REQUIRE(refused.error().code == ErrorCode::Version);
}

TEST_CASE("format/states: nothing a kept document refers to is deleted") {
  FolderFixture fixture{"states-collect"};
  const DocumentImage withFirst = imageOf(named("first state"));
  const DocumentImage withSecond = imageOf(withOtherState("second state"));
  const std::string first = withFirst.stateRefs().front();
  const std::string second = withSecond.stateRefs().front();
  provideReferenced(fixture.folder, withFirst);
  REQUIRE(fixture.folder.storeState(otherStateContent) == second);
  const auto inUndoHistory = fixture.folder.storeState("held by undo");
  const auto forQueuedSave = fixture.folder.storeState("for a queued save");
  const auto orphan = fixture.folder.storeState("referred to by nothing");
  REQUIRE(inUndoHistory.has_value());
  REQUIRE(forQueuedSave.has_value());
  REQUIRE(orphan.has_value());
  // Things in state/ that are not state files are not LINA's to delete.
  const std::vector<std::string> foreign{
      std::string(64, 'G') + ".bin",
      "notes.tmp",
      "notes.txt",
      "short.bin",
  };
  for (const std::string &name : foreign) {
    writeText(fixture.folder.stateFolder() / name, "mine");
  }
  const std::vector<std::string> keep{*inUndoHistory, *forQueuedSave};
  const std::vector<std::string> nothing;
  const auto stateFiles = [&fixture, &foreign] {
    std::vector<std::string> names = filesUnder(fixture.folder.stateFolder());
    std::erase_if(names, [&foreign](const std::string &name) {
      return std::ranges::find(foreign, name) != foreign.end();
    });
    return names;
  };
  const auto sorted = [](std::vector<std::string> names) {
    std::ranges::sort(names);
    return names;
  };

  // Saving itself deletes nothing, however often.
  REQUIRE(fixture.folder.save(withFirst).has_value());
  REQUIRE(fixture.folder.save(withSecond).has_value());
  REQUIRE(stateFiles().size() == 5);

  // Collected with the session's list: only the orphan goes. The first
  // state stays for the backup, the second for the document.
  auto removed = fixture.folder.collectStates(keep);
  REQUIRE(removed.has_value());
  REQUIRE(*removed == std::vector<std::string>{*orphan});
  REQUIRE(stateFiles() == sorted({first, second, keep.front(), keep.back()}));
  REQUIRE(fixture.folder.loadState(*inUndoHistory).has_value());

  // The recovery copy counts as well: once the backup no longer refers to
  // the first state, the copy still does.
  REQUIRE(fixture.folder.save(withSecond).has_value());
  REQUIRE(fixture.folder.writeRecovery(withFirst).has_value());
  removed = fixture.folder.collectStates(keep);
  REQUIRE(removed.has_value());
  REQUIRE(removed->empty());
  REQUIRE(fixture.folder.discardRecovery().has_value());
  removed = fixture.folder.collectStates(keep);
  REQUIRE(removed.has_value());
  REQUIRE(*removed == std::vector<std::string>{first});

  // What the session no longer lists goes with the next collection.
  removed = fixture.folder.collectStates(nothing);
  REQUIRE(removed.has_value());
  REQUIRE(sorted(*removed) == sorted(keep));
  REQUIRE(stateFiles() == std::vector<std::string>{second});
  std::vector<std::string> left = foreign;
  left.push_back(second);
  REQUIRE(filesUnder(fixture.folder.stateFolder()) == sorted(left));
  // Again: nothing left to do.
  REQUIRE(fixture.folder.collectStates(nothing)->empty());
}

TEST_CASE("format/states: nothing is deleted while anything is in doubt") {
  FolderFixture fixture{"states-doubt"};
  const DocumentImage image = imageOf(named("document"));
  provideReferenced(fixture.folder, image);
  REQUIRE(fixture.folder.save(image).has_value());
  REQUIRE(fixture.folder.save(image).has_value());
  REQUIRE(fixture.folder.writeRecovery(image).has_value());
  const auto orphan = fixture.folder.storeState("referred to by nothing");
  REQUIRE(orphan.has_value());
  const std::vector<std::string> nothing;
  const auto orphanIsThere = [&] {
    return std::filesystem::exists(fixture.folder.stateFolder() / *orphan);
  };

  // A document that is there and cannot be read may need any state file.
  for (const std::filesystem::path &unreadable : {
           fixture.folder.documentPath(),
           fixture.folder.recoveryDocumentPath(),
           fixture.folder.backupFolder() / "project.lina",
           fixture.folder.backupFolder() / "project.unreadable.lina",
           fixture.folder.backupFolder() / "project.v0.lina",
       }) {
    CAPTURE(unreadable.string());
    const bool wasThere = std::filesystem::exists(unreadable);
    const std::string before = wasThere ? readText(unreadable) : std::string{};
    writeText(unreadable, "{]");
    const auto removed = fixture.folder.collectStates(nothing);
    REQUIRE(!removed.has_value());
    REQUIRE(removed.error().where == unreadable.string());
    REQUIRE(removed.error().message.find("no state file was deleted") !=
            std::string::npos);
    REQUIRE(orphanIsThere());
    if (wasThere) {
      writeText(unreadable, before);
    } else {
      std::filesystem::remove(unreadable);
    }
  }
  // Other files in backup/ are not documents and do not matter.
  writeText(fixture.folder.backupFolder() / "readme.txt", "{]");

  // Every step may fail; the first failure stops the collection.
  fixture.files.heal();
  REQUIRE(fixture.folder.collectStates(std::vector{*orphan})->empty());
  const std::size_t steps = fixture.files.log().size();
  for (std::size_t fault = 0; fault < steps; ++fault) {
    CAPTURE(fault);
    fixture.files.failAt(fault, FaultMode::FailOnce);
    REQUIRE(!fixture.folder.collectStates(nothing).has_value());
    REQUIRE(orphanIsThere());
  }
  // Then the removal itself.
  fixture.files.failAt(steps, FaultMode::FailOnce);
  const auto failed = fixture.folder.collectStates(nothing);
  REQUIRE(fixture.files.faultHit());
  REQUIRE(!failed.has_value());
  REQUIRE(failed.error().message == "injected failure");
  REQUIRE(orphanIsThere());
  fixture.files.heal();
  REQUIRE(*fixture.folder.collectStates(nothing) ==
          std::vector<std::string>{*orphan});

  // No state folder, no backup folder, no documents: nothing to do.
  std::filesystem::remove_all(fixture.folder.stateFolder());
  std::filesystem::remove_all(fixture.folder.backupFolder());
  std::filesystem::remove_all(fixture.folder.recoveryFolder());
  std::filesystem::remove(fixture.folder.documentPath());
  REQUIRE(fixture.folder.collectStates(nothing)->empty());
}

} // namespace lina::format::testing
