// SPDX-License-Identifier: GPL-3.0-or-later
//
// New, open, save and save as through the project folder: the lock, the
// recovery copy, folders that cannot be locked or written to, and saves
// that fail. Every folder is a scratch folder of the test case.
#include <catch2/catch_test_macros.hpp>

#include "lina/format/document_io.hpp"
#include "lina/format/project_folder.hpp"
#include "lina/model/command.hpp"
#include "lina/ui/document_controller.hpp"
#include "lina/ui/fault_text.hpp"
#include "lina/ui/testing/fake_playback.hpp"
#include "lina/ui/testing/headless.hpp"
#include "lina/ui/testing/scripted_file_system.hpp"
#include "support/builders.hpp"

#include <QSignalSpy>
#include <QUrl>

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

using lina::ui::DocumentController;
using lina::ui::testing::FakePlayback;
using lina::ui::testing::ScriptedFileSystem;
namespace format = lina::format;
namespace model = lina::model;

namespace {

model::AddTrack audioTrack(std::string name) {
  return {.kind = model::TrackKind::Audio, .name = std::move(name), .index = 0};
}

QUrl url(const std::filesystem::path &path) {
  return QUrl::fromLocalFile(QString::fromStdString(path.string()));
}

/// One running copy of LINA as far as files go.
struct Instance {
  FakePlayback playback;
  ScriptedFileSystem files;
  DocumentController controller{playback, files, "LINA test"};
};

/// A project folder saved by one instance that has since closed it, with a
/// track called "Drums".
std::filesystem::path savedProject(const std::filesystem::path &scratch) {
  const auto folder = scratch / "Song";
  Instance writer;
  REQUIRE(writer.controller.perform("Add Audio Track", audioTrack("Drums")));
  REQUIRE(writer.controller.saveAsFolder(folder));
  return folder;
}

} // namespace

TEST_CASE("save as makes the project folder and keeps the project there",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-save-as");
  Instance lina;
  DocumentController &controller = lina.controller;
  const QSignalSpy location(&controller, &DocumentController::locationChanged);
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));

  // Saving an untitled project asks for a folder first.
  const QSignalSpy folderWanted(&controller, &DocumentController::folderWanted);
  CHECK(!controller.save());
  CHECK(folderWanted.count() == 1);
  controller.folderChosen(url(scratch / "Song"));

  CHECK(controller.title() == "Song");
  CHECK(controller.folder() == scratch / "Song");
  CHECK(controller.folderPath() ==
        QString::fromStdString((scratch / "Song").string()));
  CHECK(!controller.modified());
  CHECK(location.count() == 1);
  CHECK(controller.notice().isEmpty());
  CHECK(std::filesystem::is_regular_file(scratch / "Song" / "project.lina"));
  CHECK(std::filesystem::is_regular_file(scratch / "Song" / "view.lina"));

  // The folder is locked while the project is open.
  format::PosixFileSystem files;
  format::ProjectFolder other(files, scratch / "Song");
  auto lock = other.acquireLock(format::currentProcessNote("other"));
  REQUIRE(lock.has_value());
  CHECK(lock->state == format::LockState::HeldElsewhere);

  // A later save keeps the previous version as a backup.
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Bass")));
  CHECK(controller.modified());
  CHECK(controller.save());
  CHECK(!controller.modified());
  CHECK(std::filesystem::is_regular_file(scratch / "Song" / "backup" /
                                         "project.lina"));

  // Save as into its own folder is a save.
  CHECK(controller.saveAsFolder(scratch / "Song"));
}

TEST_CASE("save as refuses a folder that holds files", "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-not-empty");
  std::filesystem::create_directories(scratch / "Busy");
  {
    std::ofstream(scratch / "Busy" / "notes.txt") << "mine\n";
  }
  Instance lina;
  CHECK(!lina.controller.saveAsFolder(scratch / "Busy"));
  CHECK(lina.controller.noticeIsError());
  CHECK(lina.controller.notice().contains("already holds files"));
  CHECK(!lina.controller.folder().has_value());
  CHECK(!std::filesystem::exists(scratch / "Busy" / "project.lina"));

  // A folder whose parent is missing cannot be made.
  CHECK(!lina.controller.saveAsFolder(scratch / "missing" / "Song"));
  CHECK(lina.controller.notice().contains("could not be made"));
}

TEST_CASE("an open project comes back as it was saved", "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-open");
  const auto folder = savedProject(scratch);
  Instance lina;
  const QSignalSpy reset(&lina.controller, &DocumentController::projectReset);
  const QSignalSpy view(&lina.controller, &DocumentController::viewStateLoaded);

  lina.controller.folderChosen(url(folder));
  // Not asked for: an answer that comes anyway is ignored.
  CHECK(lina.controller.project().tracks.empty());
  CHECK(reset.count() == 0);

  lina.controller.openFolder(folder);
  REQUIRE(lina.controller.project().tracks.size() == 1);
  CHECK(lina.controller.project().tracks.at(0).name == "Drums");
  CHECK(lina.controller.title() == "Song");
  CHECK(!lina.controller.modified());
  CHECK(!lina.controller.canUndo());
  CHECK(reset.count() == 1);
  CHECK(view.count() == 1);
  CHECK(lina.playback.projectsSet == 2);
  CHECK(lina.playback.project == lina.controller.project());
  CHECK(lina.playback.mediaFolder == folder / "media");
}

TEST_CASE("a project open elsewhere is reported, not opened", "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-held");
  const auto folder = savedProject(scratch);
  Instance first;
  first.controller.openFolder(folder);
  REQUIRE(first.controller.folder() == folder);

  Instance second;
  second.controller.openFolder(folder);
  CHECK(!second.controller.folder().has_value());
  CHECK(second.controller.project().tracks.empty());
  CHECK(second.controller.noticeIsError());
  CHECK(second.controller.notice().contains("already open"));
  CHECK(second.controller.notice().contains("LINA test"));
  CHECK(!second.controller.prompt()->active());
}

TEST_CASE("a folder without a project is reported", "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-empty");
  std::filesystem::create_directories(scratch / "Empty");
  Instance lina;
  lina.controller.openFolder(scratch / "Empty");
  CHECK(!lina.controller.folder().has_value());
  CHECK(lina.controller.noticeIsError());
  CHECK(lina.controller.notice().contains("could not be opened"));
  // The lock it took was let go again.
  format::PosixFileSystem files;
  format::ProjectFolder folder(files, scratch / "Empty");
  auto lock = folder.acquireLock(format::currentProcessNote("other"));
  REQUIRE(lock.has_value());
  CHECK(lock->state == format::LockState::Acquired);

  // A folder that is not there at all.
  lina.controller.openFolder(scratch / "Nowhere");
  CHECK(lina.controller.noticeIsError());
  CHECK(!lina.controller.folder().has_value());
}

TEST_CASE("a recovery copy is offered, and kept until the project is saved",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-recovery");
  const auto folder = savedProject(scratch);
  {
    // A session that crashed after adding a second track.
    format::PosixFileSystem files;
    format::ProjectFolder project(files, folder);
    auto loaded = project.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->document.perform("Add", audioTrack("Bass")).has_value());
    auto image = format::writeDocument(loaded->document, "LINA test");
    REQUIRE(image.has_value());
    REQUIRE(project.writeRecovery(*image).has_value());
  }

  SECTION("declined") {
    Instance lina;
    lina.controller.openFolder(folder);
    lina::ui::Prompt &prompt = *lina.controller.prompt();
    REQUIRE(prompt.active());
    CHECK(prompt.title().contains("Song"));
    CHECK(prompt.choices().size() == 2);
    // Nothing is open while the question waits.
    CHECK(!lina.controller.folder().has_value());
    prompt.answer(prompt.cancelChoice());
    CHECK(lina.controller.project().tracks.size() == 1);
    CHECK(!lina.controller.modified());
    // Declining keeps the copy.
    CHECK(std::filesystem::exists(folder / "recovery" / "project.lina"));
  }

  SECTION("taken") {
    Instance lina;
    lina.controller.openFolder(folder);
    REQUIRE(lina.controller.prompt()->active());
    lina.controller.prompt()->answer(0);
    CHECK(lina.controller.project().tracks.size() == 2);
    // It differs from what is saved, before any edit.
    CHECK(lina.controller.modified());
    CHECK(std::filesystem::exists(folder / "recovery" / "project.lina"));
    CHECK(lina.controller.save());
    CHECK(!lina.controller.modified());
    CHECK(!std::filesystem::exists(folder / "recovery" / "project.lina"));
  }
}

TEST_CASE("a folder without locks opens only when the user says so",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-no-locks");
  const auto folder = savedProject(scratch);
  Instance lina;
  lina.files.answerLocksWith(ScriptedFileSystem::Lock::Unsupported);

  lina.controller.openFolder(folder);
  REQUIRE(lina.controller.prompt()->active());
  CHECK(lina.controller.prompt()->text().contains("without locks"));
  lina.controller.prompt()->answer(lina.controller.prompt()->cancelChoice());
  CHECK(!lina.controller.folder().has_value());

  lina.controller.openFolder(folder);
  lina.controller.prompt()->answer(0);
  CHECK(lina.controller.folder() == folder);
  CHECK(lina.controller.project().tracks.size() == 1);
  CHECK(!lina.controller.readOnly());
}

TEST_CASE("a folder that cannot be written to opens for looking only",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-read-only");
  const auto folder = savedProject(scratch);
  Instance lina;
  lina.files.answerLocksWith(ScriptedFileSystem::Lock::ReadOnly);
  const QSignalSpy folderWanted(&lina.controller,
                                &DocumentController::folderWanted);

  lina.controller.openFolder(folder);
  REQUIRE(lina.controller.prompt()->active());
  lina.controller.prompt()->answer(lina.controller.prompt()->cancelChoice());
  CHECK(!lina.controller.folder().has_value());

  lina.controller.openFolder(folder);
  lina.controller.prompt()->answer(0);
  CHECK(lina.controller.folder() == folder);
  CHECK(lina.controller.readOnly());
  // Saving goes elsewhere.
  CHECK(!lina.controller.save());
  CHECK(folderWanted.count() == 1);
  CHECK(lina.controller.notice().contains("Choose another folder"));

  // Unsaved changes on quit: "Save" asks for a folder too.
  REQUIRE(lina.controller.perform("Add Audio Track", audioTrack("Bass")));
  lina.controller.requestQuit();
  lina.controller.prompt()->answer(0);
  CHECK(folderWanted.count() == 2);
}

TEST_CASE("a save that fails says so and leaves the project unsaved",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-disk-full");
  Instance lina;
  REQUIRE(lina.controller.saveAsFolder(scratch / "Song"));
  REQUIRE(lina.controller.perform("Add Audio Track", audioTrack("Drums")));
  lina.files.fillDisk();
  CHECK(!lina.controller.save());
  CHECK(lina.controller.modified());
  CHECK(lina.controller.noticeIsError());
  CHECK(lina.controller.notice().contains("last saved version is unchanged"));

  // Save as fails the same way, and the project stays where it was.
  CHECK(!lina.controller.saveAsFolder(scratch / "Other"));
  CHECK(lina.controller.folder() == scratch / "Song");

  // Unsaved changes and "Save" on quit: a failed save does not quit.
  const QSignalSpy quit(&lina.controller, &DocumentController::quitApproved);
  lina.controller.requestQuit();
  lina.controller.prompt()->answer(0);
  CHECK(quit.count() == 0);
}

TEST_CASE("save as cannot yet take media and plugin state along",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-media");
  Instance lina;
  REQUIRE(lina.controller.saveAsFolder(scratch / "Song"));
  const model::MediaReference media{
      .id = model::MediaId{0},
      .path = "media/kick-0011aabb.wav",
      .sizeBytes = 4,
      .contentHash = std::string(64, 'a'),
      .sampleRate = 48000,
  };
  REQUIRE(lina.controller.perform("Import", model::AddMedia{media}));
  // In its own folder the save goes through, and reports the missing file.
  CHECK(lina.controller.save());
  CHECK(!lina.controller.noticeIsError());
  CHECK(lina.controller.notice().contains("kick-0011aabb.wav"));
  // Elsewhere it is refused.
  CHECK(!lina.controller.saveAsFolder(scratch / "Copy"));
  CHECK(lina.controller.notice().contains("cannot copy yet"));
  CHECK(lina.controller.folder() == scratch / "Song");
}

TEST_CASE("every refusal of the document has words for the user",
          "[ui][files]") {
  std::set<QString> texts;
  for (int fault = 0; fault <= static_cast<int>(model::Fault::NothingToRedo);
       ++fault) {
    const QString text = lina::ui::faultText(static_cast<model::Fault>(fault));
    CHECK(!text.isEmpty());
    texts.insert(text);
  }
  // NoTransaction and TransactionOpen share theirs.
  CHECK(texts.size() == static_cast<std::size_t>(model::Fault::NothingToRedo));
  CHECK(!lina::ui::faultText(static_cast<model::Fault>(200)).isEmpty());
}

TEST_CASE("open asks for a folder and opens the one chosen", "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-open-chosen");
  const auto folder = savedProject(scratch);
  Instance lina;
  const QSignalSpy folderWanted(&lina.controller,
                                &DocumentController::folderWanted);
  lina.controller.requestOpen();
  REQUIRE(folderWanted.count() == 1);
  lina.controller.folderChosen(url(folder));
  CHECK(lina.controller.folder() == folder);
  CHECK(lina.controller.project().tracks.size() == 1);
}

TEST_CASE("a project the engine refuses opens all the same, and says so",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-engine");
  const auto folder = savedProject(scratch);
  Instance lina;
  lina.playback.projectFailures = 1;
  lina.controller.openFolder(folder);
  CHECK(lina.controller.folder() == folder);
  CHECK(lina.controller.project().tracks.size() == 1);
  CHECK(lina.controller.noticeIsError());
  CHECK(lina.controller.notice().contains("refused the project"));
}

TEST_CASE("plugin state, and what a save cannot tidy up, are reported",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-state");
  Instance lina;
  REQUIRE(lina.controller.saveAsFolder(scratch / "Song"));
  REQUIRE(lina.controller.perform("Add Audio Track", audioTrack("Drums")));
  model::Device effect = model::testing::makeEffect("Echo");
  effect.stateRef = std::string(64, 'b') + ".bin";
  REQUIRE(lina.controller.perform(
      "Add Device",
      model::AddDevice{.track = lina.controller.project().tracks.at(0).id,
                       .device = effect,
                       .index = 0}));

  // The state file is missing, and the view state cannot be written.
  lina.files.failCreating("view.lina");
  CHECK(lina.controller.save());
  CHECK(!lina.controller.modified());
  CHECK(!lina.controller.noticeIsError());
  CHECK(lina.controller.notice().contains(QString(64, 'b') + ".bin"));
  CHECK(lina.controller.notice().contains("view state"));

  // The backup cannot be given its name: the save stands, and says so.
  lina.files.stopFailingCreation();
  lina.files.failRenamingTo("backup/project.lina");
  REQUIRE(lina.controller.perform("Add Audio Track", audioTrack("Bass")));
  CHECK(lina.controller.save());
  CHECK(!lina.controller.modified());
  CHECK(lina.controller.notice().contains("refused the rename"));

  // Save As cannot take the state along yet.
  CHECK(!lina.controller.saveAsFolder(scratch / "Copy"));
  CHECK(lina.controller.notice().contains("cannot copy yet"));
}

TEST_CASE("a project the file format refuses is not saved", "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-refused");
  Instance lina;
  const model::MediaReference media{
      .id = model::MediaId{0},
      .path = "../outside.wav",
      .sizeBytes = 4,
      .contentHash = std::string(64, 'a'),
      .sampleRate = 48000,
  };
  REQUIRE(lina.controller.perform("Import", model::AddMedia{media}));
  CHECK(!lina.controller.saveAsFolder(scratch / "Song"));
  CHECK(lina.controller.noticeIsError());
  CHECK(lina.controller.notice().contains("could not be saved"));
  CHECK(lina.controller.modified());
}

TEST_CASE("save as into a folder that cannot be written to", "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-save-as-ro");
  Instance lina;
  lina.files.answerLocksWith(ScriptedFileSystem::Lock::ReadOnly);
  CHECK(!lina.controller.saveAsFolder(scratch / "Song"));
  CHECK(lina.controller.noticeIsError());
  CHECK(lina.controller.notice().contains("cannot be saved into"));
  CHECK(!lina.controller.folder().has_value());
}

TEST_CASE("a folder chosen while a question waits is dropped", "[ui][files]") {
  const auto scratch =
      lina::ui::testing::enterHeadless("ui-files-chosen-asked");
  const auto folder = savedProject(scratch);
  Instance lina;
  // Opening this folder would ask a question of its own.
  lina.files.answerLocksWith(ScriptedFileSystem::Lock::Unsupported);
  // The folder dialog is up for Open...
  lina.controller.requestOpen();
  // ...when another request asks "Save changes?".
  REQUIRE(lina.controller.perform("Add Audio Track", audioTrack("Bass")));
  lina.controller.requestQuit();
  REQUIRE(lina.controller.prompt()->active());
  const QString asked = lina.controller.prompt()->title();
  CHECK_NOTHROW(lina.controller.folderChosen(url(folder)));
  CHECK(lina.controller.prompt()->title() == asked);
  CHECK(!lina.controller.folder().has_value());
  CHECK_NOTHROW(lina.controller.openFolder(folder));
  CHECK(lina.controller.prompt()->title() == asked);
}

TEST_CASE("a Save As that failed lets nothing go on after a later save",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-stale-save");
  std::filesystem::create_directories(scratch / "Busy");
  {
    std::ofstream(scratch / "Busy" / "notes.txt") << "mine\n";
  }

  SECTION("quit") {
    Instance lina;
    const QSignalSpy quit(&lina.controller, &DocumentController::quitApproved);
    REQUIRE(lina.controller.perform("Add Audio Track", audioTrack("Drums")));
    lina.controller.requestQuit();
    lina.controller.prompt()->answer(0);
    lina.controller.folderChosen(url(scratch / "Busy"));
    CHECK(lina.controller.noticeIsError());
    // Later, an ordinary Save As.
    lina.controller.requestSaveAs();
    lina.controller.folderChosen(url(scratch / "Song"));
    CHECK(!lina.controller.modified());
    CHECK(quit.count() == 0);
  }

  SECTION("new project") {
    Instance lina;
    REQUIRE(lina.controller.perform("Add Audio Track", audioTrack("Drums")));
    lina.controller.requestNew();
    lina.controller.prompt()->answer(0);
    lina.controller.folderChosen(url(scratch / "Busy"));
    // Later, Ctrl+S on the still untitled project.
    CHECK(!lina.controller.save());
    lina.controller.folderChosen(url(scratch / "Song"));
    CHECK(lina.controller.folder() == scratch / "Song");
    CHECK(lina.controller.project().tracks.size() == 1);
  }
}

TEST_CASE("saving in the middle of a gesture saves what is shown",
          "[ui][files]") {
  const auto scratch =
      lina::ui::testing::enterHeadless("ui-files-gesture-save");
  Instance lina;
  REQUIRE(lina.controller.saveAsFolder(scratch / "Song"));
  const model::Tempo fast =
      model::Tempo::fromHundredths(14500).value_or(model::Tempo{});
  REQUIRE(lina.controller.beginGesture("Change Tempo"));
  REQUIRE(lina.controller.applyInGesture(
      model::SetProjectProperty{model::TempoValue{fast}}));
  CHECK(lina.controller.save());
  // The drag is cancelled afterwards: it was kept by the save.
  lina.controller.cancelGesture();
  CHECK(lina.controller.project().tempo == fast);
  CHECK(!lina.controller.modified());
  CHECK(lina.controller.undoText() == "Undo Change Tempo");

  format::PosixFileSystem files;
  auto saved = format::ProjectFolder(files, scratch / "Song").load();
  REQUIRE(saved.has_value());
  CHECK(saved->document.project().tempo == fast);

  // Save As in the middle of a gesture keeps it too.
  REQUIRE(lina.controller.beginGesture("Change Tempo"));
  REQUIRE(lina.controller.applyInGesture(
      model::SetProjectProperty{model::TempoValue{model::Tempo{}}}));
  lina.controller.requestSaveAs();
  CHECK(!lina.controller.inGesture());
  CHECK(lina.controller.modified());
}

TEST_CASE("opening the project that is open already says so", "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-open-again");
  Instance lina;
  REQUIRE(lina.controller.saveAsFolder(scratch / "Song"));
  REQUIRE(lina.controller.perform("Add Audio Track", audioTrack("Drums")));
  const QSignalSpy reset(&lina.controller, &DocumentController::projectReset);
  // Written another way, it is the same folder.
  lina.controller.openFolder(scratch / "." / "Song");
  CHECK(!lina.controller.noticeIsError());
  CHECK(lina.controller.notice().contains("already open in this window"));
  CHECK(!lina.controller.notice().contains("another copy"));
  CHECK(reset.count() == 0);
  CHECK(lina.controller.project().tracks.size() == 1);
  CHECK(lina.controller.modified());
}

TEST_CASE("a lock whose holder cannot be read is reported without a name",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-anonymous");
  const auto folder = savedProject(scratch);
  Instance lina;
  lina.files.answerLocksWith(ScriptedFileSystem::Lock::HeldElsewhere);
  lina.controller.openFolder(folder);
  CHECK(!lina.controller.folder().has_value());
  CHECK(lina.controller.notice().contains(
      "already open in another copy of LINA. Close it"));

  // Taken between the check for an empty folder and the save: refused.
  CHECK(!lina.controller.saveAsFolder(scratch / "Other"));
  CHECK(lina.controller.notice().contains("cannot be saved into"));
}

TEST_CASE("save as refuses what is not a folder, and the folder it came from "
          "when that cannot be written to",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-not-folder");
  const auto folder = savedProject(scratch);
  {
    std::ofstream(scratch / "notes.txt") << "mine\n";
  }
  Instance lina;
  CHECK(!lina.controller.saveAsFolder(scratch / "notes.txt"));
  CHECK(lina.controller.notice().contains("could not be made"));

  lina.files.answerLocksWith(ScriptedFileSystem::Lock::ReadOnly);
  lina.controller.openFolder(folder);
  lina.controller.prompt()->answer(0);
  REQUIRE(lina.controller.readOnly());
  CHECK(!lina.controller.saveAsFolder(folder));
  CHECK(lina.controller.notice().contains("already holds files"));
}

TEST_CASE("plugin state on a return track or the master stays with its folder",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-state-ret");
  Instance lina;
  REQUIRE(lina.controller.saveAsFolder(scratch / "Song"));
  model::Device effect = model::testing::makeEffect("Echo");
  effect.stateRef = std::string(64, 'c') + ".bin";

  SECTION("return track") {
    REQUIRE(lina.controller.perform(
        "Add Return Track", model::AddTrack{.kind = model::TrackKind::Return,
                                            .name = "Space",
                                            .index = 0}));
    REQUIRE(lina.controller.perform(
        "Add Device",
        model::AddDevice{.track = lina.controller.project().returns.at(0).id,
                         .device = effect,
                         .index = 0}));
  }
  SECTION("master") {
    REQUIRE(lina.controller.perform(
        "Add Device",
        model::AddDevice{.track = lina.controller.project().master.id,
                         .device = effect,
                         .index = 0}));
  }
  CHECK(!lina.controller.saveAsFolder(scratch / "Copy"));
  CHECK(lina.controller.notice().contains("cannot copy yet"));
}

TEST_CASE("save as takes an empty folder that is there already",
          "[ui][files]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-files-empty-dir");
  std::filesystem::create_directories(scratch / "Made");
  Instance lina;
  CHECK(lina.controller.saveAsFolder(scratch / "Made"));
  CHECK(lina.controller.folder() == scratch / "Made");
  CHECK(std::filesystem::is_regular_file(scratch / "Made" / "project.lina"));
}
