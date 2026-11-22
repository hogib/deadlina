// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell: every command registered once and kept up to date, and the
// keyboard of a real (offscreen) window going through the registry.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lina/format/project_folder.hpp"
#include "lina/ui/shell.hpp"
#include "lina/ui/shortcut_dispatcher.hpp"
#include "lina/ui/testing/fake_playback.hpp"
#include "lina/ui/testing/headless.hpp"

#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QUrl>

#include <filesystem>
#include <memory>
#include <set>

using lina::ui::Shell;
using lina::ui::Workspace;
using lina::ui::testing::FakePlayback;

namespace {

struct Fixture {
  FakePlayback playback;
  lina::format::PosixFileSystem files;
  Shell shell{playback, files, "LINA test"};
};

/// Sends a key press to a window as the platform would.
void press(QQuickWindow &window, QKeyCombination key, bool repeat = false) {
  QKeyEvent event(QEvent::KeyPress, key.key(), key.keyboardModifiers(),
                  QString(), repeat);
  QCoreApplication::sendEvent(&window, &event);
}

} // namespace

TEST_CASE("every command has a name, an explanation and a unique key",
          "[ui][shell]") {
  Fixture fixture;
  const auto &commands = *fixture.shell.commands();
  const QStringList ids = commands.ids();
  CHECK(ids.size() >= 30);
  std::set<QString> names;
  for (const QString &id : ids) {
    CAPTURE(id.toStdString());
    const auto *command = commands.command(id);
    REQUIRE(command != nullptr);
    CHECK(!command->name().isEmpty());
    CHECK(command->description().endsWith(QLatin1Char('.')));
    names.insert(command->name());
  }
  CHECK(names.size() == static_cast<std::size_t>(ids.size()));
  // Linux desktop conventions (principle 4).
  CHECK(commands.commandForKey(Qt::ControlModifier | Qt::Key_Z) == "edit.undo");
  CHECK(commands.commandForKey(Qt::ControlModifier | Qt::ShiftModifier |
                               Qt::Key_Z) == "edit.redo");
  CHECK(commands.commandForKey(Qt::ControlModifier | Qt::Key_S) ==
        "project.save");
  CHECK(commands.commandForKey(Qt::ControlModifier | Qt::Key_Q) == "app.quit");
  CHECK(commands.commandForKey(QKeyCombination(Qt::Key_Space)) ==
        "transport.toggle");
}

TEST_CASE("undo and redo commands carry the names of their steps",
          "[ui][shell]") {
  Fixture fixture;
  Shell &shell = fixture.shell;
  const auto &commands = *shell.commands();
  CHECK(commands.name("edit.undo") == "Undo");
  CHECK(!commands.isEnabled("edit.undo"));
  CHECK(!shell.run("edit.undo"));

  CHECK(shell.run("track.addAudio"));
  CHECK(shell.run("track.addMidi"));
  CHECK(shell.run("track.addAudio"));
  const auto &tracks = shell.document()->project().tracks;
  REQUIRE(tracks.size() == 3);
  CHECK(tracks.at(0).name == "Audio 1");
  CHECK(tracks.at(1).name == "MIDI 1");
  CHECK(tracks.at(2).name == "Audio 2");
  CHECK(shell.tracks()->count() == 3);
  CHECK(commands.name("edit.undo") == "Undo Add Audio Track");
  CHECK(commands.isEnabled("edit.undo"));

  CHECK(shell.run("edit.undo"));
  CHECK(commands.name("edit.undo") == "Undo Add MIDI Track");
  CHECK(commands.name("edit.redo") == "Redo Add Audio Track");
  CHECK(shell.run("edit.redo"));
  CHECK(!commands.isEnabled("edit.redo"));
  CHECK(fixture.playback.project == shell.document()->project());
}

TEST_CASE("the window title names the project and shows unsaved changes",
          "[ui][shell]") {
  Fixture fixture;
  Shell &shell = fixture.shell;
  const QSignalSpy title(&shell, &Shell::windowTitleChanged);
  CHECK(shell.windowTitle() == "Untitled – LINA");
  shell.run("track.addAudio");
  CHECK(shell.windowTitle() == "*Untitled – LINA");
  CHECK(title.count() == 1);
}

TEST_CASE("commands are enabled when they can do something", "[ui][shell]") {
  Fixture fixture;
  fixture.playback.output = false;
  fixture.shell.transport()->refreshOutput();
  Shell &shell = fixture.shell;
  const auto &commands = *shell.commands();

  // Without an output the transport commands still run, and say why
  // nothing plays.
  CHECK(commands.isEnabled("transport.toggle"));
  CHECK(commands.isEnabled("transport.play"));
  CHECK(commands.isEnabled("transport.continue"));
  CHECK(commands.isEnabled("transport.stop"));
  for (const char *id :
       {"transport.toggle", "transport.play", "transport.continue"}) {
    CAPTURE(id);
    shell.document()->dismissNotice();
    CHECK(shell.run(id));
    CHECK(!shell.transport()->playing());
    CHECK(shell.document()->notice().contains("--no-audio"));
  }
  fixture.playback.output = true;
  shell.transport()->refreshOutput();
  CHECK(commands.isEnabled("transport.toggle"));
  CHECK(shell.run("transport.play"));
  CHECK(shell.transport()->playing());
  CHECK(shell.run("transport.toggle"));
  CHECK(!shell.transport()->playing());
  CHECK(shell.run("transport.continue"));
  CHECK(shell.run("transport.stop"));
  CHECK(!shell.transport()->playing());

  CHECK(!commands.isEnabled("view.both"));
  CHECK(shell.run("view.sessionOnly"));
  CHECK(!commands.isEnabled("view.sessionOnly"));
  CHECK(!commands.isEnabled("view.dividerUp"));
  CHECK(commands.isEnabled("view.both"));
  CHECK(shell.run("view.arrangementOnly"));
  CHECK(shell.workspace()->focusedPane() == Workspace::Pane::Arrangement);
  CHECK(shell.run("view.session"));
  CHECK(shell.workspace()->views() == Workspace::Views::Both);
  CHECK(shell.run("view.arrangement"));
  CHECK(shell.run("view.nextPane"));
  CHECK(shell.workspace()->focusedPane() == Workspace::Pane::Session);
  CHECK(shell.run("view.previousPane"));
  CHECK(shell.workspace()->focusedPane() == Workspace::Pane::Arrangement);
  CHECK(shell.run("view.dividerDown"));
  CHECK(shell.workspace()->split() > Workspace::defaultSplit);
  CHECK(shell.run("view.dividerUp"));
  CHECK(shell.run("view.both") == false);

  CHECK(shell.run("view.larger"));
  CHECK(shell.appearance()->scale() > 1.0);
  CHECK(shell.run("view.actualSize"));
  CHECK(shell.run("view.smaller"));
  CHECK(shell.appearance()->scale() < 1.0);
  CHECK(!commands.isEnabled("view.followDesktop"));
  CHECK(shell.run("view.dark"));
  CHECK(shell.appearance()->dark());
  CHECK(!commands.isEnabled("view.dark"));
  CHECK(shell.run("view.light"));
  CHECK(!shell.appearance()->dark());
  CHECK(shell.run("view.followDesktop"));
}

TEST_CASE("commands the window carries out are handed to it", "[ui][shell]") {
  Fixture fixture;
  Shell &shell = fixture.shell;
  const QSignalSpy list(&shell, &Shell::commandListRequested);
  const QSignalSpy tempo(&shell, &Shell::tempoEntryRequested);
  const QSignalSpy folder(shell.document(),
                          &lina::ui::DocumentController::folderWanted);
  const QSignalSpy quit(shell.document(),
                        &lina::ui::DocumentController::quitApproved);
  CHECK(shell.run("app.commands"));
  CHECK(shell.run("transport.tempo"));
  CHECK(shell.run("project.open"));
  CHECK(shell.run("project.saveAs"));
  CHECK(shell.run("project.save"));
  CHECK(shell.run("project.new"));
  CHECK(shell.run("app.quit"));
  CHECK(list.count() == 1);
  CHECK(tempo.count() == 1);
  CHECK(folder.count() == 3);
  CHECK(quit.count() == 1);
}

TEST_CASE("no command runs while a question waits", "[ui][shell]") {
  Fixture fixture;
  Shell &shell = fixture.shell;
  shell.run("track.addAudio");
  CHECK(shell.run("project.new"));
  REQUIRE(shell.document()->prompt()->active());
  CHECK(shell.commands()->suspended());
  CHECK(!shell.run("track.addAudio"));
  shell.document()->prompt()->answer(2);
  CHECK(!shell.commands()->suspended());
  CHECK(shell.run("track.addAudio"));
}

TEST_CASE("the pane that had the keys comes back with the project",
          "[ui][shell]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-shell-view-state");
  {
    Fixture writer;
    writer.shell.workspace()->setFocusedPane(Workspace::Pane::Arrangement);
    REQUIRE(writer.shell.document()->saveAsFolder(scratch / "Song"));
  }
  Fixture reader;
  REQUIRE(reader.shell.workspace()->focusedPane() == Workspace::Pane::Session);
  reader.shell.document()->openFolder(scratch / "Song");
  CHECK(reader.shell.workspace()->focusedPane() ==
        Workspace::Pane::Arrangement);
  CHECK(reader.shell.windowTitle() == "Song – LINA");
}

TEST_CASE("the keyboard of a window goes through the registry",
          "[ui][shell][keys]") {
  lina::ui::testing::enterHeadless("ui-shell-keys");
  const lina::ui::testing::GuiApplication qt;
  Fixture fixture;
  auto &commands = *fixture.shell.commands();

  QQuickWindow window;
  auto dispatcher = std::make_unique<lina::ui::ShortcutDispatcher>();
  const QSignalSpy registry(dispatcher.get(),
                            &lina::ui::ShortcutDispatcher::registryChanged);
  dispatcher->setRegistry(&commands);
  dispatcher->setRegistry(&commands);
  CHECK(registry.count() == 1);
  CHECK(dispatcher->registry() == &commands);
  dispatcher->setParentItem(window.contentItem());

  // A text field to type into.
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData("import QtQuick\nTextInput { width: 100; height: 20 }",
                    QUrl());
  const std::unique_ptr<QObject> created(component.create());
  auto *field = qobject_cast<QQuickItem *>(created.get());
  REQUIRE(field != nullptr);
  field->setParentItem(window.contentItem());

  // A command's key runs it and is used up.
  press(window, Qt::ControlModifier | Qt::Key_T);
  CHECK(fixture.shell.tracks()->count() == 1);
  press(window, QKeyCombination(Qt::Key_Space));
  CHECK(fixture.shell.transport()->playing());
  // Held down, it runs once.
  press(window, QKeyCombination(Qt::Key_Space), true);
  CHECK(fixture.shell.transport()->playing());
  press(window, QKeyCombination(Qt::Key_Space));
  CHECK(!fixture.shell.transport()->playing());
  // A disabled command's key goes on to the items: with no drag going on,
  // Escape is free for the focused control.
  commands.setEnabled("track.addAudio", false);
  press(window, Qt::ControlModifier | Qt::Key_T);
  CHECK(fixture.shell.tracks()->count() == 1);
  commands.setEnabled("track.addAudio", true);
  REQUIRE(!commands.isEnabled("edit.cancelDrag"));
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  CHECK(!dispatcher->eventFilter(&window, &escape));
  QKeyEvent addTrack(QEvent::KeyPress, Qt::Key_T, Qt::ControlModifier);
  CHECK(dispatcher->eventFilter(&window, &addTrack));
  CHECK(fixture.shell.tracks()->count() == 2);
  fixture.shell.document()->undo();
  // During a drag, Escape cancels it.
  fixture.shell.workspace()->beginDividerDrag();
  fixture.shell.workspace()->dragDivider(100.0, 400.0);
  CHECK(commands.isEnabled("edit.cancelDrag"));
  press(window, QKeyCombination(Qt::Key_Escape));
  CHECK(!fixture.shell.workspace()->dividerDragging());
  CHECK(fixture.shell.workspace()->split() == Workspace::defaultSplit);

  // While typing, the field keeps its keys; other commands still run.
  field->forceActiveFocus();
  REQUIRE(window.activeFocusItem() == field);
  press(window, QKeyCombination(Qt::Key_Space));
  CHECK(!fixture.shell.transport()->playing());
  press(window, Qt::ControlModifier | Qt::Key_T);
  CHECK(fixture.shell.tracks()->count() == 2);
  press(window, Qt::ControlModifier | Qt::Key_Z);
  CHECK(fixture.shell.tracks()->count() == 2);

  // A multi-line text area keeps its keys too.
  QQmlComponent areaComponent(&engine);
  areaComponent.setData("import QtQuick\nTextEdit { width: 100; height: 40 }",
                        QUrl());
  const std::unique_ptr<QObject> areaObject(areaComponent.create());
  auto *area = qobject_cast<QQuickItem *>(areaObject.get());
  REQUIRE(area != nullptr);
  area->setParentItem(window.contentItem());
  area->forceActiveFocus();
  REQUIRE(window.activeFocusItem() == area);
  press(window, QKeyCombination(Qt::Key_Space));
  CHECK(!fixture.shell.transport()->playing());
  area->setFocus(false);

  // While a question waits, its controls have the keyboard.
  field->setFocus(false);
  commands.setSuspended(true);
  press(window, Qt::ControlModifier | Qt::Key_T);
  CHECK(fixture.shell.tracks()->count() == 2);
  commands.setSuspended(false);

  // Moved to another window, it listens there and no longer here.
  QQuickWindow other;
  dispatcher->setParentItem(other.contentItem());
  press(window, Qt::ControlModifier | Qt::Key_T);
  CHECK(fixture.shell.tracks()->count() == 2);
  press(other, Qt::ControlModifier | Qt::Key_T);
  CHECK(fixture.shell.tracks()->count() == 3);

  // Gone, it listens nowhere; nor does one without a registry.
  dispatcher.reset();
  press(other, Qt::ControlModifier | Qt::Key_T);
  lina::ui::ShortcutDispatcher empty;
  empty.setParentItem(other.contentItem());
  press(other, Qt::ControlModifier | Qt::Key_T);
  CHECK(fixture.shell.tracks()->count() == 3);
}

TEST_CASE("the saved view is the one on the screen", "[ui][shell]") {
  const auto scratch = lina::ui::testing::enterHeadless("ui-shell-saved-view");
  Fixture fixture;
  Shell &shell = fixture.shell;
  lina::format::PosixFileSystem files;
  const auto saved = [&] {
    return lina::format::ProjectFolder(files, scratch / "Song")
        .loadView()
        .value_or(lina::format::ViewState{})
        .view;
  };
  // Saved with the arrangement, the session then gets the keys, and
  // another project comes: the view state starts afresh for it.
  shell.workspace()->setFocusedPane(Workspace::Pane::Arrangement);
  REQUIRE(shell.document()->saveAsFolder(scratch / "Song"));
  CHECK(saved() == lina::format::MainView::Arrangement);
  shell.workspace()->setFocusedPane(Workspace::Pane::Session);
  REQUIRE(shell.document()->save());
  CHECK(saved() == lina::format::MainView::Session);
  // A new project starts with a view state of its own; the screen keeps
  // the arrangement.
  shell.workspace()->setFocusedPane(Workspace::Pane::Arrangement);
  shell.run("project.new");
  REQUIRE(shell.workspace()->focusedPane() == Workspace::Pane::Arrangement);
  std::filesystem::remove_all(scratch / "Song");
  REQUIRE(shell.document()->saveAsFolder(scratch / "Song"));
  CHECK(saved() == lina::format::MainView::Arrangement);
}

TEST_CASE("Escape cancels the drag that is going on", "[ui][shell]") {
  Fixture fixture;
  Shell &shell = fixture.shell;
  const auto &commands = *shell.commands();
  CHECK(!commands.isEnabled("edit.cancelDrag"));

  shell.transport()->beginTempoDrag();
  CHECK(shell.transport()->tempoDragging());
  CHECK(commands.isEnabled("edit.cancelDrag"));
  shell.transport()->dragTempo(40.0, false);
  REQUIRE(shell.transport()->tempoText() == "130.00");
  CHECK(shell.run("edit.cancelDrag"));
  CHECK(shell.transport()->tempoText() == "120.00");
  CHECK(!shell.document()->canUndo());
  CHECK(!shell.transport()->tempoDragging());
  CHECK(!commands.isEnabled("edit.cancelDrag"));
  // Moving and letting go afterwards changes nothing.
  shell.transport()->dragTempo(80.0, false);
  shell.transport()->endTempoDrag();
  CHECK(shell.transport()->tempoText() == "120.00");
  CHECK(!shell.document()->canUndo());

  shell.workspace()->setSplit(0.4);
  shell.workspace()->beginDividerDrag();
  shell.workspace()->beginDividerDrag();
  shell.workspace()->dragDivider(100.0, 500.0);
  CHECK(shell.workspace()->split() == Catch::Approx(0.6));
  CHECK(commands.isEnabled("edit.cancelDrag"));
  CHECK(shell.run("edit.cancelDrag"));
  CHECK(shell.workspace()->split() == Catch::Approx(0.4));
  shell.workspace()->dragDivider(100.0, 500.0);
  shell.workspace()->endDividerDrag();
  CHECK(shell.workspace()->split() == Catch::Approx(0.4));

  // A drag that ends normally stays.
  shell.workspace()->beginDividerDrag();
  shell.workspace()->dragDivider(-50.0, 500.0);
  shell.workspace()->endDividerDrag();
  CHECK(shell.workspace()->split() == Catch::Approx(0.3));
  CHECK(!commands.isEnabled("edit.cancelDrag"));
}

TEST_CASE("a new project ends a tempo drag", "[ui][shell]") {
  Fixture fixture;
  Shell &shell = fixture.shell;
  shell.transport()->beginTempoDrag();
  shell.transport()->dragTempo(40.0, false);
  shell.run("project.new");
  // The drag was kept, so there were changes to ask about.
  REQUIRE(shell.document()->prompt()->active());
  shell.document()->prompt()->answer(1);
  CHECK(!shell.transport()->tempoDragging());
  CHECK(!shell.commands()->isEnabled("edit.cancelDrag"));
  CHECK(shell.transport()->tempoText() == "120.00");
}
