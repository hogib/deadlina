// SPDX-License-Identifier: GPL-3.0-or-later
//
// The document controller: commands, undo and redo with their names, change
// sets reaching the engine and the models, gestures, and the questions
// asked before unsaved changes are given up.
#include <catch2/catch_test_macros.hpp>

#include "lina/model/command.hpp"
#include "lina/ui/document_controller.hpp"
#include "lina/ui/prompt.hpp"
#include "lina/ui/testing/fake_playback.hpp"
#include "lina/ui/testing/headless.hpp"

#include <QSignalSpy>
#include <QUrl>

#include <filesystem>
#include <stdexcept>
#include <vector>

using lina::ui::DocumentController;
using lina::ui::testing::FakePlayback;
namespace model = lina::model;

namespace {

model::AddTrack audioTrack(std::string name, std::size_t index = 0) {
  return {
      .kind = model::TrackKind::Audio,
      .name = std::move(name),
      .index = index,
  };
}

model::SetProjectProperty tempo(std::int32_t hundredths) {
  const model::Tempo value =
      model::Tempo::fromHundredths(hundredths).value_or(model::Tempo{});
  return {model::TempoValue{value}};
}

/// A controller over a fake engine and the real file system, and every
/// change set it announced.
struct Fixture {
  Fixture() {
    QObject::connect(&controller, &DocumentController::projectChanged,
                     [this](const model::ChangeSet &changes) {
                       announced.push_back(changes);
                     });
  }

  FakePlayback playback;
  lina::format::PosixFileSystem files;
  DocumentController controller{playback, files, "LINA test"};
  std::vector<model::ChangeSet> announced;
};

} // namespace

TEST_CASE("a new controller holds an empty, untitled, saved project",
          "[ui][document]") {
  Fixture fixture;
  const DocumentController &controller = fixture.controller;
  CHECK(controller.project() == model::makeEmptyProject());
  CHECK(controller.title() == "Untitled");
  CHECK(controller.folderPath().isEmpty());
  CHECK(!controller.folder().has_value());
  CHECK(!controller.modified());
  CHECK(!controller.readOnly());
  CHECK(!controller.canUndo());
  CHECK(!controller.canRedo());
  CHECK(controller.undoText() == "Undo");
  CHECK(controller.redoText() == "Redo");
  // The engine has the project from the start.
  CHECK(fixture.playback.projectsSet == 1);
  CHECK(fixture.playback.project == controller.project());
  CHECK(fixture.playback.mediaFolder.is_absolute());
}

TEST_CASE("each change set reaches the engine and then the models, in order",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;

  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));
  REQUIRE(controller.perform("Change Tempo", tempo(9600)));
  const std::vector<model::Command> two{audioTrack("Bass"), audioTrack("Keys")};
  REQUIRE(controller.perform("Add Two Tracks", two));

  REQUIRE(fixture.announced.size() == 3);
  CHECK(fixture.playback.changeSets == fixture.announced);
  CHECK(fixture.playback.project == controller.project());
  CHECK(controller.project().tracks.size() == 3);
  // The two tracks of one step are one change set.
  CHECK(fixture.announced.at(2).changes().size() == 2);

  controller.undo();
  controller.undo();
  controller.redo();
  REQUIRE(fixture.announced.size() == 6);
  CHECK(fixture.playback.changeSets == fixture.announced);
  CHECK(fixture.playback.project == controller.project());
  CHECK(controller.project().tempo.hundredths() == 9600);
  CHECK(controller.project().tracks.size() == 1);
}

TEST_CASE("undo and redo are named after the step they act on",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  const QSignalSpy history(&controller, &DocumentController::historyChanged);
  const QSignalSpy modified(&controller, &DocumentController::modifiedChanged);

  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));
  CHECK(controller.undoText() == "Undo Add Audio Track");
  CHECK(controller.redoText() == "Redo");
  CHECK(controller.modified());
  CHECK(modified.count() == 1);
  CHECK(history.count() == 1);

  REQUIRE(controller.perform("Change Tempo", tempo(13000)));
  CHECK(controller.undoText() == "Undo Change Tempo");
  // Already modified: no new signal.
  CHECK(modified.count() == 1);

  controller.undo();
  CHECK(controller.undoText() == "Undo Add Audio Track");
  CHECK(controller.redoText() == "Redo Change Tempo");
  controller.undo();
  CHECK(controller.undoText() == "Undo");
  CHECK(controller.redoText() == "Redo Add Audio Track");
  // Back where it started: what is saved.
  CHECK(!controller.modified());
  CHECK(modified.count() == 2);
  // Nothing left: undo does nothing.
  controller.undo();
  CHECK(fixture.announced.size() == 4);

  controller.redo();
  controller.redo();
  controller.redo();
  CHECK(controller.undoText() == "Undo Change Tempo");
  CHECK(!controller.canRedo());

  // A new step after an undo drops what could be redone.
  controller.undo();
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Keys", 1)));
  CHECK(!controller.canRedo());
  CHECK(controller.redoText() == "Redo");
}

TEST_CASE("a command that is refused changes nothing and says why",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  const QSignalSpy notice(&controller, &DocumentController::noticeChanged);

  const model::RemoveTrack missing{model::TrackId{999}};
  CHECK(!controller.perform("Remove Track", missing));
  CHECK(controller.project() == model::makeEmptyProject());
  CHECK(fixture.announced.empty());
  CHECK(fixture.playback.changeSets.empty());
  CHECK(!controller.canUndo());
  CHECK(controller.noticeIsError());
  CHECK(controller.notice().contains("Remove Track"));
  CHECK(controller.notice().contains("does not exist"));
  CHECK(notice.count() == 1);

  controller.dismissNotice();
  CHECK(controller.notice().isEmpty());
  controller.dismissNotice();
  CHECK(notice.count() == 2);
}

TEST_CASE("an edit that changes nothing leaves no step", "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  // The tempo is 120 already.
  REQUIRE(controller.perform("Change Tempo", tempo(12000)));
  CHECK(!controller.canUndo());
  CHECK(!controller.modified());
  CHECK(fixture.announced.empty());
}

TEST_CASE("a gesture reaches the engine as it goes and is one undo step",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;

  REQUIRE(controller.beginGesture("Change Tempo"));
  CHECK(controller.inGesture());
  // Nothing else edits meanwhile.
  CHECK(!controller.beginGesture("Another"));
  CHECK(!controller.perform("Add Audio Track", audioTrack("Drums")));
  CHECK(!controller.canUndo());
  REQUIRE(controller.applyInGesture(tempo(12100)));
  REQUIRE(controller.applyInGesture(tempo(12200)));
  REQUIRE(controller.applyInGesture(tempo(12300)));
  // A refused command in a gesture changes nothing and the gesture goes on.
  CHECK(!controller.applyInGesture(model::RemoveTrack{model::TrackId{999}}));
  CHECK(fixture.playback.changeSets.size() == 3);
  CHECK(
      fixture.playback.project.value_or(model::Project{}).tempo.hundredths() ==
      12300);
  controller.endGesture();
  CHECK(!controller.inGesture());
  CHECK(controller.undoText() == "Undo Change Tempo");
  CHECK(controller.modified());

  controller.undo();
  CHECK(controller.project().tempo.hundredths() == 12000);
  CHECK(!controller.canUndo());
  CHECK(
      fixture.playback.project.value_or(model::Project{}).tempo.hundredths() ==
      12000);

  // Outside a gesture these do nothing.
  CHECK(!controller.applyInGesture(tempo(5000)));
  controller.endGesture();
  controller.cancelGesture();
  CHECK(controller.project().tempo.hundredths() == 12000);
}

TEST_CASE("a cancelled gesture is taken back and leaves no step",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  REQUIRE(controller.beginGesture("Change Tempo"));
  REQUIRE(controller.applyInGesture(tempo(15000)));
  controller.cancelGesture();
  CHECK(controller.project().tempo.hundredths() == 12000);
  CHECK(
      fixture.playback.project.value_or(model::Project{}).tempo.hundredths() ==
      12000);
  CHECK(!controller.canUndo());
  CHECK(!controller.modified());
  CHECK(fixture.announced.size() == 2);

  // A gesture that moves nothing leaves no step either.
  REQUIRE(controller.beginGesture("Change Tempo"));
  controller.endGesture();
  CHECK(!controller.canUndo());
}

TEST_CASE("an engine that fails on a change set is reported, and the edit "
          "stands",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  fixture.playback.failures = 1;
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));
  CHECK(controller.project().tracks.size() == 1);
  CHECK(fixture.announced.size() == 1);
  CHECK(controller.noticeIsError());
  CHECK(controller.notice().contains("failed on purpose"));
}

TEST_CASE("quitting with nothing unsaved needs no question", "[ui][document]") {
  Fixture fixture;
  const QSignalSpy quit(&fixture.controller, &DocumentController::quitApproved);
  fixture.controller.requestQuit();
  CHECK(quit.count() == 1);
  CHECK(!fixture.controller.prompt()->active());
}

TEST_CASE("unsaved changes are offered for saving before they are given up",
          "[ui][document]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-document-unsaved");
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  lina::ui::Prompt &prompt = *controller.prompt();
  const QSignalSpy quit(&controller, &DocumentController::quitApproved);
  const QSignalSpy folderWanted(&controller, &DocumentController::folderWanted);
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));

  // Cancel: nothing happens.
  controller.requestQuit();
  REQUIRE(prompt.active());
  CHECK(prompt.title().contains("Untitled"));
  CHECK(prompt.choices().size() == 3);
  prompt.answer(prompt.cancelChoice());
  CHECK(!prompt.active());
  CHECK(quit.count() == 0);
  CHECK(controller.modified());

  // Save, with no folder yet: a folder is asked for, and quitting waits for
  // the save.
  controller.requestQuit();
  prompt.answer(0);
  REQUIRE(folderWanted.count() == 1);
  CHECK(folderWanted.at(0).at(0).value<DocumentController::FolderPurpose>() ==
        DocumentController::FolderPurpose::SaveAs);
  CHECK(quit.count() == 0);
  controller.folderChosen(
      QUrl::fromLocalFile(QString::fromStdString((scratch / "Song").string())));
  CHECK(quit.count() == 1);
  CHECK(!controller.modified());
  CHECK(controller.title() == "Song");

  // Save, with a folder: saved, then approved.
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Bass")));
  controller.requestQuit();
  prompt.answer(0);
  CHECK(quit.count() == 2);
  CHECK(!controller.modified());

  // Do not save: approved, and the changes stay unsaved.
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Keys")));
  controller.requestQuit();
  prompt.answer(1);
  CHECK(quit.count() == 3);
  CHECK(controller.modified());
}

TEST_CASE("a folder dialog that is cancelled drops what waited for it",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  const QSignalSpy quit(&controller, &DocumentController::quitApproved);
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));
  controller.requestQuit();
  controller.prompt()->answer(0);
  controller.folderChoiceCancelled();
  // An answer that comes anyway goes nowhere.
  controller.folderChosen(QUrl::fromLocalFile("/nonexistent/folder"));
  CHECK(quit.count() == 0);
  CHECK(controller.modified());
}

TEST_CASE("a new project replaces the open one once its changes are dealt "
          "with",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  const QSignalSpy aboutToReset(&controller,
                                &DocumentController::projectAboutToReset);
  const QSignalSpy reset(&controller, &DocumentController::projectReset);
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));

  controller.requestNew();
  REQUIRE(controller.prompt()->active());
  controller.prompt()->answer(1);
  CHECK(aboutToReset.count() == 1);
  CHECK(reset.count() == 1);
  CHECK(controller.project() == model::makeEmptyProject());
  CHECK(!controller.modified());
  CHECK(!controller.canUndo());
  CHECK(fixture.playback.projectsSet == 2);
  CHECK(fixture.playback.project == controller.project());

  // Unmodified: at once.
  controller.requestNew();
  CHECK(reset.count() == 2);
}

TEST_CASE("open asks about unsaved changes, then for a folder",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  const QSignalSpy folderWanted(&controller, &DocumentController::folderWanted);
  controller.requestOpen();
  REQUIRE(folderWanted.count() == 1);
  CHECK(folderWanted.at(0).at(0).value<DocumentController::FolderPurpose>() ==
        DocumentController::FolderPurpose::Open);

  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));
  controller.requestOpen();
  CHECK(folderWanted.count() == 1);
  controller.prompt()->answer(1);
  CHECK(folderWanted.count() == 2);

  controller.requestSaveAs();
  CHECK(folderWanted.count() == 3);
}

TEST_CASE("a gesture that repeats itself or applies nothing",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  REQUIRE(controller.beginGesture("Change Tempo"));
  REQUIRE(controller.applyInGesture(tempo(12100)));
  // The same value again changes nothing and reaches nobody.
  REQUIRE(controller.applyInGesture(tempo(12100)));
  CHECK(fixture.announced.size() == 1);
  controller.endGesture();
  CHECK(controller.canUndo());

  // Cancelled before anything was applied: nothing to take back.
  REQUIRE(controller.beginGesture("Change Tempo"));
  controller.cancelGesture();
  CHECK(fixture.announced.size() == 1);
  CHECK(controller.undoText() == "Undo Change Tempo");
}

TEST_CASE("a question is asked once and answered once", "[ui][document]") {
  lina::ui::Prompt prompt;
  int answered = -1;
  const auto remember = [&answered](int choice) { answered = choice; };

  // Nothing waits: an answer goes nowhere.
  prompt.answer(0);
  CHECK(answered == -1);

  CHECK_THROWS_AS(prompt.ask("Title", "Text", {}, 0, remember),
                  std::invalid_argument);
  CHECK_THROWS_AS(prompt.ask("Title", "Text", {"Yes"}, 1, remember),
                  std::invalid_argument);
  CHECK_THROWS_AS(prompt.ask("Title", "Text", {"Yes"}, -1, remember),
                  std::invalid_argument);
  CHECK_THROWS_AS(prompt.ask("Title", "Text", {"Yes"}, 0, nullptr),
                  std::invalid_argument);
  CHECK(!prompt.active());

  const QSignalSpy changed(&prompt, &lina::ui::Prompt::changed);
  prompt.ask("Title", "Text", {"Yes", "No"}, 1, remember);
  CHECK(prompt.active());
  CHECK(prompt.title() == "Title");
  CHECK(prompt.text() == "Text");
  CHECK(prompt.choices() == QStringList{"Yes", "No"});
  CHECK(prompt.cancelChoice() == 1);
  CHECK_THROWS_AS(prompt.ask("Again", "Text", {"Yes"}, 0, remember),
                  std::logic_error);

  // An answer out of range is the one that changes nothing.
  prompt.answer(7);
  CHECK(answered == 1);
  prompt.ask("Title", "Text", {"Yes", "No"}, 1, remember);
  answered = -1;
  prompt.answer(-1);
  CHECK(answered == 1);
  CHECK(!prompt.active());
  CHECK(prompt.title().isEmpty());
  CHECK(prompt.choices().isEmpty());
  CHECK(changed.count() == 4);

  // The question is over before its continuation runs, which may ask the
  // next one.
  prompt.ask("First", "Text", {"Go"}, 0, [&](int /*choice*/) {
    prompt.ask("Second", "Text", {"Yes", "No"}, 1, remember);
  });
  prompt.answer(0);
  REQUIRE(prompt.active());
  CHECK(prompt.title() == "Second");
  prompt.answer(0);
  CHECK(answered == 0);
}

TEST_CASE("a second request while a question waits is dropped",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  lina::ui::Prompt &prompt = *controller.prompt();
  const QSignalSpy quit(&controller, &DocumentController::quitApproved);
  const QSignalSpy folderWanted(&controller, &DocumentController::folderWanted);
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));

  // Ctrl+Q, then the window's close button: one question, still the first.
  controller.requestQuit();
  REQUIRE(prompt.active());
  const QString asked = prompt.title();
  CHECK_NOTHROW(controller.requestQuit());
  CHECK_NOTHROW(controller.requestNew());
  CHECK_NOTHROW(controller.requestOpen());
  CHECK(prompt.title() == asked);
  CHECK(folderWanted.count() == 0);
  CHECK(quit.count() == 0);

  // The answer still counts, once.
  prompt.answer(1);
  CHECK(quit.count() == 1);
  CHECK(!prompt.active());
}

TEST_CASE("a gesture counts as a change, and undo waits for it",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));
  controller.undo();
  REQUIRE(!controller.modified());
  REQUIRE(controller.canRedo());
  const QSignalSpy history(&controller, &DocumentController::historyChanged);
  const QSignalSpy modified(&controller, &DocumentController::modifiedChanged);

  REQUIRE(controller.beginGesture("Change Tempo"));
  // The undo and redo buttons learn that they wait.
  CHECK(history.count() == 1);
  CHECK(!controller.canUndo());
  CHECK(!controller.canRedo());
  CHECK(!controller.modified());
  REQUIRE(controller.applyInGesture(tempo(14500)));
  CHECK(controller.modified());
  CHECK(modified.count() == 1);
  // Undo and redo do nothing while it lasts.
  controller.undo();
  controller.redo();
  CHECK(controller.project().tempo.hundredths() == 14500);
  CHECK(controller.project().tracks.empty());

  controller.cancelGesture();
  CHECK(!controller.modified());
  CHECK(modified.count() == 2);
  CHECK(history.count() == 2);
  CHECK(controller.canRedo());
  controller.redo();
  CHECK(controller.project().tracks.size() == 1);
}

TEST_CASE("leaving in the middle of a gesture keeps it and asks about it",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  const QSignalSpy quit(&controller, &DocumentController::quitApproved);
  REQUIRE(controller.beginGesture("Change Tempo"));
  REQUIRE(controller.applyInGesture(tempo(14500)));

  controller.requestQuit();
  CHECK(!controller.inGesture());
  CHECK(controller.undoText() == "Undo Change Tempo");
  REQUIRE(controller.prompt()->active());
  CHECK(quit.count() == 0);
  controller.prompt()->answer(controller.prompt()->cancelChoice());

  // Undoing it then takes back the whole gesture.
  controller.undo();
  CHECK(controller.project().tempo.hundredths() == 12000);
  CHECK(!controller.modified());
}

TEST_CASE("undo and redo that exist still wait for a gesture",
          "[ui][document]") {
  Fixture fixture;
  DocumentController &controller = fixture.controller;
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Drums")));
  REQUIRE(controller.perform("Add Audio Track", audioTrack("Bass")));
  controller.undo();
  REQUIRE(controller.canUndo());
  REQUIRE(controller.canRedo());
  REQUIRE(controller.beginGesture("Change Tempo"));
  CHECK(!controller.canUndo());
  CHECK(!controller.canRedo());
  controller.endGesture();
  CHECK(controller.canUndo());
  CHECK(controller.canRedo());
}
