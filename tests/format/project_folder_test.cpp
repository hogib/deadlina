// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project folder (docs/design/16-project-file-format-encoding.md): its
// layout, saving in the order the doc gives, the backup, the recovery copy
// and the view state.
#include "support/documents.hpp"
#include "support/folder_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/stat.h>

namespace lina::format::testing {

namespace {

using Log = std::vector<std::string>;

model::Document renamed(std::string sceneName) {
  model::Document document = minimalDocument();
  mustApply(document, model::RenameScene{
                          .scene = document.project().scenes.front().id,
                          .name = std::move(sceneName),
                      });
  return document;
}

std::uint32_t permissionsOf(const std::filesystem::path &file) {
  struct stat status{};
  REQUIRE(::stat(file.c_str(), &status) == 0);
  return status.st_mode & 07777U;
}

} // namespace

TEST_CASE("format/folder: the layout is the one the design gives") {
  PosixFileSystem files;
  const ProjectFolder folder{files, "/music/My Song"};
  REQUIRE(folder.root() == "/music/My Song");
  REQUIRE(folder.documentPath() == "/music/My Song/project.lina");
  REQUIRE(folder.viewPath() == "/music/My Song/view.lina");
  REQUIRE(folder.mediaFolder() == "/music/My Song/media");
  REQUIRE(folder.stateFolder() == "/music/My Song/state");
  REQUIRE(folder.backupFolder() == "/music/My Song/backup");
  REQUIRE(folder.recoveryFolder() == "/music/My Song/recovery");
  REQUIRE(folder.recoveryDocumentPath() ==
          "/music/My Song/recovery/project.lina");
  REQUIRE(folder.cacheFolder() == "/music/My Song/cache");
  REQUIRE(folder.lockPath() == "/music/My Song/.lock");
}

TEST_CASE("format/folder: creating makes the folders that are always there") {
  FolderFixture fixture{"folder-create"};
  REQUIRE(fixture.created);
  for (const char *name : {"media", "state", "cache"}) {
    REQUIRE(std::filesystem::is_directory(fixture.root / name));
  }
  // Present only when needed.
  for (const char *name : {"backup", "recovery", ".lock", "project.lina"}) {
    REQUIRE(!std::filesystem::exists(fixture.root / name));
  }
  // Again changes nothing and succeeds.
  writeText(fixture.root / "media" / "kept.wav", "audio");
  REQUIRE(fixture.folder.create().has_value());
  REQUIRE(readText(fixture.root / "media" / "kept.wav") == "audio");
  REQUIRE(fixture.files.log() == Log{"mkdir .", "mkdir media", "mkdir state",
                                     "mkdir cache", "sync .", "sync .."});

  // The parent must be there.
  PosixFileSystem files;
  ProjectFolder orphan{files, fixture.root / "no" / "such" / "Song"};
  const auto failed = orphan.create();
  REQUIRE(!failed.has_value());
  REQUIRE(failed.error().code == ErrorCode::NotFound);

  // Every step of creating can fail and is reported.
  for (std::size_t step = 0; step < 6; ++step) {
    fixture.files.failAt(step, FaultMode::FailOnce);
    REQUIRE(!fixture.folder.create().has_value());
  }
  fixture.files.failAt(6, FaultMode::FailOnce);
  REQUIRE(fixture.folder.create().has_value());
}

TEST_CASE("format/folder: a folder named without a path is made in place") {
  const auto scratch = freshScratch("folder-relative");
  const auto before = std::filesystem::current_path();
  std::filesystem::current_path(scratch);
  PosixFileSystem files;
  ProjectFolder folder{files, "Song"};
  const auto created = folder.create();
  std::filesystem::current_path(before);
  REQUIRE(created.has_value());
  REQUIRE(std::filesystem::is_directory(scratch / "Song" / "media"));
}

TEST_CASE("format/folder: a saved document loads as it was") {
  FolderFixture fixture{"folder-save-load"};
  const auto missing = fixture.folder.load();
  REQUIRE(!missing.has_value());
  REQUIRE(missing.error().code == ErrorCode::NotFound);

  const model::Document document = everythingDocument();
  const DocumentImage image = imageOf(document);
  provideReferenced(fixture.folder, image);
  const auto report = fixture.folder.save(image);
  REQUIRE(report.has_value());
  REQUIRE(report->backupName.empty());
  REQUIRE(report->afterSwapProblems.empty());
  REQUIRE(readText(fixture.folder.documentPath()) == image.bytes());

  const auto loaded = fixture.folder.load();
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->document.project() == document.project());
  REQUIRE(loaded->formatRead == formatVersion);
  REQUIRE(loaded->writtenBy == testWriter);
  REQUIRE(loaded->migrationWarnings.empty());
  REQUIRE(!std::filesystem::exists(fixture.folder.backupFolder()));
}

TEST_CASE("format/folder: a document that is damaged on disk does not load") {
  FolderFixture fixture{"folder-load-damaged"};
  writeText(fixture.folder.documentPath(), "{\"format\": 1");
  const auto loaded = fixture.folder.load();
  REQUIRE(!loaded.has_value());
  REQUIRE(loaded.error().code == ErrorCode::Syntax);
}

TEST_CASE("format/folder: the first save writes, flushes and renames") {
  FolderFixture fixture{"folder-first-save"};
  REQUIRE(fixture.folder.save(imageOf(minimalDocument())).has_value());
  REQUIRE(fixture.files.log() == Log{
                                     "info project.lina",
                                     "remove project.lina.tmp",
                                     "create project.lina.tmp",
                                     "write project.lina.tmp",
                                     "fsync project.lina.tmp",
                                     "rename project.lina.tmp",
                                     "sync .",
                                     "remove recovery/project.lina",
                                     "remove recovery/project.lina.tmp",
                                     "rmdir recovery",
                                 });
  REQUIRE(filesUnder(fixture.root) == std::vector<std::string>{"project.lina"});
}

TEST_CASE("format/folder: a later save follows the order of the design") {
  FolderFixture fixture{"folder-save-order"};
  const DocumentImage image = imageOf(everythingDocument());
  provideReferenced(fixture.folder, image);
  REQUIRE(fixture.folder.save(image).has_value());
  REQUIRE(fixture.folder.writeRecovery(image).has_value());
  const std::string state = "state/" + image.stateRefs().front();

  fixture.files.heal();
  const auto report = fixture.folder.save(image);
  REQUIRE(report.has_value());
  REQUIRE(fixture.files.log() ==
          Log{
              // 2. what the document refers to is there
              "info " + state,
              "info media/kick-3f9a1c2e.wav",
              // 3. the current document, copied beside the backup
              "info project.lina",
              "mkdir backup",
              "open project.lina",
              "read project.lina",
              "read project.lina",
              "remove backup/project.lina.tmp",
              "create backup/project.lina.tmp",
              "write backup/project.lina.tmp",
              "fsync backup/project.lina.tmp",
              // 4. the new document, beside the old one
              "remove project.lina.tmp",
              "create project.lina.tmp",
              "write project.lina.tmp",
              "fsync project.lina.tmp",
              // 5. the swap
              "rename project.lina.tmp",
              "sync .",
              // 6. the backup takes its name
              "rename backup/project.lina.tmp",
              "sync backup",
              // 7. the recovery copy goes
              "remove recovery/project.lina",
              "remove recovery/project.lina.tmp",
              "rmdir recovery",
          });
  REQUIRE(report->backupName == "project.lina");
  REQUIRE(report->afterSwapProblems.empty());
  REQUIRE(!std::filesystem::exists(fixture.folder.recoveryFolder()));
  REQUIRE(temporaryFilesUnder(fixture.root).empty());
  // Saving deletes no state file, referred to or not.
  REQUIRE(std::filesystem::exists(fixture.root / state));
}

TEST_CASE("format/folder: the backup is always the previous saved document") {
  FolderFixture fixture{"folder-backup"};
  const auto backup = fixture.folder.backupFolder() / "project.lina";
  const std::vector<model::Document> versions{
      renamed("one"),
      renamed("two"),
      renamed("three"),
      renamed("four"),
  };
  std::string previous;
  for (const model::Document &document : versions) {
    const auto report = fixture.folder.save(imageOf(document));
    REQUIRE(report.has_value());
    if (previous.empty()) {
      REQUIRE(!std::filesystem::exists(backup));
    } else {
      REQUIRE(report->backupName == "project.lina");
      REQUIRE(readText(backup) == previous);
      // The backup is itself a document that loads.
      REQUIRE(readDocument(readText(backup)).has_value());
    }
    previous = imageOf(document).bytes();
    REQUIRE(readText(fixture.folder.documentPath()) == previous);
    REQUIRE(temporaryFilesUnder(fixture.root).empty());
  }
  REQUIRE(filesUnder(fixture.root) ==
          std::vector<std::string>{"backup/project.lina", "project.lina"});
}

TEST_CASE("format/folder: the document keeps the permissions it had") {
  FolderFixture fixture{"folder-permissions"};
  const mode_t before = ::umask(0022);
  REQUIRE(fixture.folder.save(imageOf(renamed("one"))).has_value());
  // A new project: what the process's defaults give.
  REQUIRE(permissionsOf(fixture.folder.documentPath()) == 0644U);

  // The user makes it private; saving keeps it so, backup included.
  REQUIRE(::chmod(fixture.folder.documentPath().c_str(), 0600) == 0);
  REQUIRE(fixture.folder.save(imageOf(renamed("two"))).has_value());
  REQUIRE(fixture.folder.save(imageOf(renamed("three"))).has_value());
  ::umask(before);
  REQUIRE(permissionsOf(fixture.folder.documentPath()) == 0600U);
  REQUIRE(permissionsOf(fixture.folder.backupFolder() / "project.lina") ==
          0600U);
}

TEST_CASE("format/folder: a symbolic link is not taken for the document") {
  FolderFixture fixture{"folder-symlink"};
  const auto elsewhere = fixture.root.parent_path() / "elsewhere.lina";
  const DocumentImage image = imageOf(renamed("linked"));
  writeText(elsewhere, image.bytes());
  std::filesystem::create_symlink(elsewhere, fixture.folder.documentPath());

  // Not loaded through, and not replaced: refused, and left as it is.
  const auto loaded = fixture.folder.load();
  REQUIRE(!loaded.has_value());
  REQUIRE(loaded.error().code == ErrorCode::NotAFile);
  const auto saved = fixture.folder.save(imageOf(renamed("other")));
  REQUIRE(!saved.has_value());
  REQUIRE(saved.error().code == ErrorCode::NotAFile);
  REQUIRE(saved.error().where == fixture.folder.documentPath().string());
  REQUIRE(std::filesystem::is_symlink(fixture.folder.documentPath()));
  REQUIRE(readText(elsewhere) == image.bytes());
  REQUIRE(temporaryFilesUnder(fixture.root).empty());

  // The same for the recovery copy and the view state.
  std::filesystem::create_directory(fixture.folder.recoveryFolder());
  std::filesystem::create_symlink(elsewhere,
                                  fixture.folder.recoveryDocumentPath());
  std::filesystem::create_symlink(elsewhere, fixture.folder.viewPath());
  REQUIRE(fixture.folder.loadRecovery().error().code == ErrorCode::NotAFile);
  REQUIRE(fixture.folder.findRecovery().error().code == ErrorCode::NotAFile);
  REQUIRE(fixture.folder.loadView().error().code == ErrorCode::NotAFile);
}

TEST_CASE("format/folder: the recovery copy is written, found and discarded") {
  FolderFixture fixture{"folder-recovery"};
  const model::Document saved = renamed("saved");
  const model::Document unsaved = renamed("unsaved");

  // Nothing to offer at first, and discarding nothing is fine.
  auto found = fixture.folder.findRecovery();
  REQUIRE(found.has_value());
  REQUIRE(!found->has_value());
  REQUIRE(fixture.folder.discardRecovery().has_value());
  REQUIRE(fixture.folder.loadRecovery().error().code == ErrorCode::NotFound);

  // A recovery copy of a project that was never saved is offered.
  fixture.files.heal();
  REQUIRE(fixture.folder.writeRecovery(imageOf(unsaved)).has_value());
  REQUIRE(fixture.files.log() == Log{
                                     "mkdir recovery",
                                     "remove recovery/project.lina.tmp",
                                     "create recovery/project.lina.tmp",
                                     "write recovery/project.lina.tmp",
                                     "fsync recovery/project.lina.tmp",
                                     "rename recovery/project.lina.tmp",
                                     "sync recovery",
                                 });
  found = fixture.folder.findRecovery();
  REQUIRE(found.has_value());
  REQUIRE(found->has_value());

  // Saving removes it.
  REQUIRE(fixture.folder.save(imageOf(saved)).has_value());
  REQUIRE(!std::filesystem::exists(fixture.folder.recoveryFolder()));
  found = fixture.folder.findRecovery();
  REQUIRE(found.has_value());
  REQUIRE(!found->has_value());

  // Written after the save: offered, whatever the clocks say. The time is
  // passed on for display only.
  REQUIRE(fixture.folder.writeRecovery(imageOf(unsaved)).has_value());
  for (const std::int64_t recoveryTime : {5'060, 5'000, 4'000}) {
    setModified(fixture.folder.documentPath(), 5'000);
    setModified(fixture.folder.recoveryDocumentPath(), recoveryTime);
    found = fixture.folder.findRecovery();
    REQUIRE(found.has_value());
    REQUIRE(found->has_value());
    const auto written =
        fixture.files.info(fixture.folder.recoveryDocumentPath());
    REQUIRE(written.has_value());
    REQUIRE(found->value_or(RecoveryInfo{}).written == written->modified);
  }
  const auto recovered = fixture.folder.loadRecovery();
  REQUIRE(recovered.has_value());
  REQUIRE(recovered->document.project() == unsaved.project());
  // Loading it does not remove it, and the saved document is untouched.
  REQUIRE(std::filesystem::exists(fixture.folder.recoveryDocumentPath()));
  REQUIRE(fixture.folder.load()->document.project() == saved.project());

  // A copy that holds exactly the saved document has nothing to offer.
  REQUIRE(fixture.folder.writeRecovery(imageOf(saved)).has_value());
  setModified(fixture.folder.recoveryDocumentPath(), 9'000);
  found = fixture.folder.findRecovery();
  REQUIRE(found.has_value());
  REQUIRE(!found->has_value());

  // Declined: discarded on request.
  REQUIRE(fixture.folder.writeRecovery(imageOf(unsaved)).has_value());
  REQUIRE(fixture.folder.discardRecovery().has_value());
  REQUIRE(!std::filesystem::exists(fixture.folder.recoveryFolder()));
  REQUIRE(fixture.folder.load()->document.project() == saved.project());
}

TEST_CASE("format/folder: failures around the recovery copy are reported") {
  FolderFixture fixture{"folder-recovery-faults"};
  const DocumentImage image = imageOf(minimalDocument());
  REQUIRE(fixture.folder.save(image).has_value());

  // Writing: any step may fail; a copy that was there stays whole.
  REQUIRE(fixture.folder.writeRecovery(image).has_value());
  const std::string before = readText(fixture.folder.recoveryDocumentPath());
  const DocumentImage other = imageOf(renamed("other"));
  for (std::size_t step = 0; step < 7; ++step) {
    CAPTURE(step);
    fixture.files.failAt(step, FaultMode::FailOnce);
    REQUIRE(!fixture.folder.writeRecovery(other).has_value());
    REQUIRE(fixture.files.faultHit());
    const std::string now = readText(fixture.folder.recoveryDocumentPath());
    REQUIRE((now == before || now == other.bytes()));
    REQUIRE(readDocument(now).has_value());
    fixture.files.heal();
    REQUIRE(temporaryFilesUnder(fixture.root).empty());
  }

  // Looking: reading either file, or asking for the time, may fail.
  fixture.files.heal();
  REQUIRE(fixture.folder.findRecovery()->has_value());
  const std::size_t steps = fixture.files.log().size();
  for (std::size_t step = 0; step < steps; ++step) {
    CAPTURE(step);
    fixture.files.failAt(step, FaultMode::FailOnce);
    const auto found = fixture.folder.findRecovery();
    REQUIRE(!found.has_value());
    REQUIRE(found.error().code == ErrorCode::Io);
  }
  fixture.files.failAt(0, FaultMode::FailOnce);
  REQUIRE(!fixture.folder.loadRecovery().has_value());
  fixture.files.failAt(0, FaultMode::FailOnce);
  REQUIRE(!fixture.folder.load().has_value());

  // Discarding: each of its steps may fail.
  for (std::size_t step = 0; step < 3; ++step) {
    fixture.files.failAt(step, FaultMode::FailOnce);
    REQUIRE(!fixture.folder.discardRecovery().has_value());
    fixture.files.heal();
    REQUIRE(fixture.folder.writeRecovery(image).has_value());
  }
  // Something else in recovery/ keeps the folder, and says so.
  writeText(fixture.folder.recoveryFolder() / "notes.txt", "mine");
  REQUIRE(!fixture.folder.discardRecovery().has_value());
  REQUIRE(readText(fixture.folder.recoveryFolder() / "notes.txt") == "mine");
}

TEST_CASE("format/folder: a recovery copy cut short by a crash is not used") {
  FolderFixture fixture{"folder-recovery-crash"};
  const DocumentImage saved = imageOf(renamed("saved"));
  const DocumentImage first = imageOf(renamed("first copy"));
  const DocumentImage second = imageOf(renamed("second copy"));
  REQUIRE(fixture.folder.save(saved).has_value());
  REQUIRE(fixture.folder.writeRecovery(first).has_value());

  // The process dies while the next copy is being written.
  fixture.files.failAt(3, FaultMode::Crash);
  REQUIRE(!fixture.folder.writeRecovery(second).has_value());
  REQUIRE(fixture.files.log().back() == "remove recovery/project.lina.tmp");
  fixture.files.heal();
  REQUIRE(temporaryFilesUnder(fixture.root) ==
          std::vector<std::string>{"recovery/project.lina.tmp"});
  REQUIRE(readText(fixture.folder.recoveryFolder() / "project.lina.tmp") ==
          second.bytes().substr(0, second.bytes().size() / 2));
  // The earlier copy is whole, and the torn file is never read.
  const auto recovered = fixture.folder.loadRecovery();
  REQUIRE(recovered.has_value());
  REQUIRE(recovered->document.project() == renamed("first copy").project());
  // The next start of the application sweeps the torn file away.
  const auto swept = fixture.folder.removeStaleTemporaries();
  REQUIRE(swept.has_value());
  REQUIRE(*swept == std::vector<std::string>{"recovery/project.lina.tmp"});
  REQUIRE(temporaryFilesUnder(fixture.root).empty());
  REQUIRE(fixture.folder.writeRecovery(second).has_value());
  REQUIRE(readText(fixture.folder.recoveryDocumentPath()) == second.bytes());
}

TEST_CASE("format/folder: view state has a file of its own") {
  FolderFixture fixture{"folder-view"};
  const auto missing = fixture.folder.loadView();
  REQUIRE(!missing.has_value());
  REQUIRE(missing.error().code == ErrorCode::NotFound);

  const DocumentImage image = imageOf(minimalDocument());
  REQUIRE(fixture.folder.save(image).has_value());
  const ViewState state{
      .view = MainView::Arrangement,
      .arrangementZoom = 2.5,
      .arrangementScroll = {4 * model::ticksPerWholeNote},
  };
  fixture.files.heal();
  REQUIRE(fixture.folder.saveView(state).has_value());
  REQUIRE(fixture.files.log() ==
          Log{"remove view.lina.tmp", "create view.lina.tmp",
              "write view.lina.tmp", "fsync view.lina.tmp",
              "rename view.lina.tmp", "sync ."});
  const auto loaded = fixture.folder.loadView();
  REQUIRE(loaded.has_value());
  REQUIRE(*loaded == state);
  // Scrolling and zooming do not change the document.
  REQUIRE(readText(fixture.folder.documentPath()) == image.bytes());
  REQUIRE(filesUnder(fixture.root) ==
          std::vector<std::string>{"project.lina", "view.lina"});

  // A view state that cannot be written leaves the old one.
  const auto refused = fixture.folder.saveView({
      .view = MainView::Session,
      .arrangementZoom = 0.0,
      .arrangementScroll = model::TickPosition{0},
  });
  REQUIRE(!refused.has_value());
  REQUIRE(refused.error().code == ErrorCode::Value);
  for (std::size_t step = 0; step < 6; ++step) {
    fixture.files.failAt(step, FaultMode::FailOnce);
    REQUIRE(!fixture.folder.saveView({}).has_value());
    fixture.files.heal();
    const auto kept = fixture.folder.loadView();
    REQUIRE(kept.has_value());
    REQUIRE((*kept == state || *kept == ViewState{}));
    REQUIRE(temporaryFilesUnder(fixture.root).empty());
  }

  writeText(fixture.folder.viewPath(), "{}");
  REQUIRE(fixture.folder.loadView().error().code == ErrorCode::Structure);
  fixture.files.failAt(0, FaultMode::FailOnce);
  REQUIRE(fixture.folder.loadView().error().code == ErrorCode::Io);
}

TEST_CASE("format/folder: files left by an interrupted run are swept away") {
  FolderFixture fixture{"folder-stale"};
  const std::string state = std::string(64, 'a') + ".bin";
  for (const char *folder : {"backup", "recovery"}) {
    std::filesystem::create_directory(fixture.root / folder);
  }
  // What LINA itself may have left, and things that only look similar.
  const std::vector<std::string> ours{
      "backup/project.lina.tmp",    "backup/project.unreadable.lina.tmp",
      "backup/project.v1.lina.tmp", "media/kick.wav.import.tmp",
      "project.lina.tmp",           "recovery/project.lina.tmp",
      "state/" + state + ".tmp",    "view.lina.tmp",
  };
  const std::vector<std::string> foreign{
      "backup/notes.tmp",      "backup/project.lina", "media/kick.tmp",
      "media/kick.wav",        "notes.tmp",           "project.lina",
      "recovery/project.lina", "state/" + state,      "state/notes.tmp",
      "state/short.bin.tmp",   "cache/anything.tmp",
  };
  for (const std::string &file : ours) {
    writeText(fixture.root / file, "torn");
  }
  for (const std::string &file : foreign) {
    writeText(fixture.root / file, "kept");
  }

  auto swept = fixture.folder.removeStaleTemporaries();
  REQUIRE(swept.has_value());
  std::ranges::sort(*swept);
  REQUIRE(*swept == ours);
  std::vector<std::string> left = foreign;
  std::ranges::sort(left);
  REQUIRE(filesUnder(fixture.root) == left);
  // Nothing to sweep is fine, and so are folders that are not there.
  std::filesystem::remove_all(fixture.folder.backupFolder());
  swept = fixture.folder.removeStaleTemporaries();
  REQUIRE(swept.has_value());
  REQUIRE(swept->empty());

  // Any step may fail.
  writeText(fixture.root / "view.lina.tmp", "torn");
  writeText(fixture.root / "media/kick.wav.import.tmp", "torn");
  fixture.files.heal();
  REQUIRE(fixture.folder.removeStaleTemporaries()->size() == 2);
  const std::size_t steps = fixture.files.log().size();
  for (std::size_t step = 0; step < steps; ++step) {
    writeText(fixture.root / "view.lina.tmp", "torn");
    writeText(fixture.root / "media/kick.wav.import.tmp", "torn");
    fixture.files.failAt(step, FaultMode::FailOnce);
    REQUIRE(!fixture.folder.removeStaleTemporaries().has_value());
  }
}

TEST_CASE("format/folder: nothing thrown underneath leaves the library") {
  FolderFixture fixture{"folder-throw"};
  const DocumentImage image = imageOf(minimalDocument());
  REQUIRE(fixture.folder.save(image).has_value());
  REQUIRE(fixture.folder.writeRecovery(image).has_value());
  const auto state = fixture.folder.storeState("state");
  REQUIRE(state.has_value());
  const auto source = fixture.root.parent_path() / "kick.wav";
  writeText(source, "kick");
  const std::vector<std::string> keep;

  const auto isInternal = [&fixture](const auto &result) {
    return fixture.files.faultHit() && !result.has_value() &&
           result.error().code == ErrorCode::Internal &&
           result.error().message == "out of memory";
  };
  const auto arm = [&fixture] { fixture.files.failAt(0, FaultMode::Throw); };
  arm();
  REQUIRE(isInternal(fixture.folder.create()));
  arm();
  REQUIRE(isInternal(fixture.folder.removeStaleTemporaries()));
  arm();
  REQUIRE(isInternal(fixture.folder.load()));
  arm();
  REQUIRE(isInternal(fixture.folder.save(image)));
  arm();
  REQUIRE(isInternal(fixture.folder.collectStates(keep)));
  arm();
  REQUIRE(isInternal(fixture.folder.writeRecovery(image)));
  arm();
  REQUIRE(isInternal(fixture.folder.findRecovery()));
  arm();
  REQUIRE(isInternal(fixture.folder.loadRecovery()));
  arm();
  REQUIRE(isInternal(fixture.folder.discardRecovery()));
  arm();
  REQUIRE(isInternal(fixture.folder.storeState("other")));
  arm();
  REQUIRE(isInternal(fixture.folder.loadState(*state)));
  arm();
  REQUIRE(isInternal(fixture.folder.importMedia(source)));
  arm();
  REQUIRE(isInternal(fixture.folder.saveView({})));
  arm();
  REQUIRE(isInternal(fixture.folder.loadView()));
  arm();
  REQUIRE(isInternal(fixture.folder.acquireLock(currentProcessNote("tests"))));
  // And the folder is as usable as before.
  fixture.files.heal();
  REQUIRE(fixture.folder.load().has_value());
}

} // namespace lina::format::testing
