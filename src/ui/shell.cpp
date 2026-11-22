// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/shell.hpp"

#include "lina/model/command.hpp"

#include <QKeySequence>

#include <algorithm>

namespace lina::ui {

namespace {

QList<QKeySequence> keys(std::initializer_list<QKeyCombination> combinations) {
  QList<QKeySequence> result;
  for (const QKeyCombination combination : combinations) {
    result.append(QKeySequence(combination));
  }
  return result;
}

} // namespace

Shell::Shell(bridge::Playback &playback, format::FileSystem &files,
             const QString &writtenBy, QObject *parent)
    : QObject(parent), document_(playback, files, writtenBy),
      transport_(document_, playback), palette_(commands_), tracks_(document_) {
  registerCommands();

  for (const auto signal : {
           &DocumentController::historyChanged,
           &DocumentController::locationChanged,
           &DocumentController::modifiedChanged,
       }) {
    connect(&document_, signal, this, &Shell::refreshCommands);
  }
  connect(&document_, &DocumentController::locationChanged, this,
          &Shell::windowTitleChanged);
  connect(&document_, &DocumentController::modifiedChanged, this,
          &Shell::windowTitleChanged);
  connect(&transport_, &TransportController::tempoDraggingChanged, this,
          &Shell::refreshCommands);
  connect(&workspace_, &Workspace::dividerDraggingChanged, this,
          &Shell::refreshCommands);
  connect(&workspace_, &Workspace::viewsChanged, this, &Shell::refreshCommands);
  connect(&appearance_, &Appearance::scaleChanged, this,
          &Shell::refreshCommands);
  connect(&appearance_, &Appearance::modeChanged, this,
          &Shell::refreshCommands);
  connect(document_.prompt(), &Prompt::changed, this,
          [this] { commands_.setSuspended(document_.prompt()->active()); });

  // The view that has the keys is saved with the project, and comes back
  // with it.
  document_.setViewSource([this](format::ViewState state) {
    state.view = workspace_.focusedPane() == Workspace::Pane::Session
                     ? format::MainView::Session
                     : format::MainView::Arrangement;
    return state;
  });
  connect(&document_, &DocumentController::viewStateLoaded, this, [this] {
    workspace_.setFocusedPane(document_.viewState().view ==
                                      format::MainView::Session
                                  ? Workspace::Pane::Session
                                  : Workspace::Pane::Arrangement);
  });

  refreshCommands();
}

QString Shell::windowTitle() const {
  const QString title = tr("%1 – LINA").arg(document_.title());
  return document_.modified() ? QStringLiteral("*") + title : title;
}

bool Shell::run(const QString &id) { return commands_.trigger(id); }

void Shell::addTrack(model::TrackKind kind, const QString &name) {
  const auto &tracks = document_.project().tracks;
  const auto sameKind = std::ranges::count_if(
      tracks, [kind](const model::Track &track) { return track.kind == kind; });
  const QString trackName = kind == model::TrackKind::Midi
                                ? tr("MIDI %1").arg(sameKind + 1)
                                : tr("Audio %1").arg(sameKind + 1);
  document_.perform(name, model::AddTrack{
                              .kind = kind,
                              .name = trackName.toStdString(),
                              .index = tracks.size(),
                          });
}

void Shell::registerCommands() {
  const auto ctrl = Qt::ControlModifier;
  const auto shift = Qt::ShiftModifier;

  commands_.add({
      .id = QStringLiteral("app.commands"),
      .name = tr("Show Command List"),
      .description = tr("Lists every command with its shortcut; type to find "
                        "one and press Enter to run it."),
      .shortcuts = keys({ctrl | Qt::Key_K}),
      .action = [this] { Q_EMIT commandListRequested(); },
  });

  // --- Project ---
  commands_.add({
      .id = QStringLiteral("project.new"),
      .name = tr("New Project"),
      .description = tr("Starts an empty project, after offering to save "
                        "this one."),
      .shortcuts = keys({ctrl | Qt::Key_N}),
      .action = [this] { document_.requestNew(); },
  });
  commands_.add({
      .id = QStringLiteral("project.open"),
      .name = tr("Open Project…"),
      .description = tr("Opens a project folder, after offering to save this "
                        "project."),
      .shortcuts = keys({ctrl | Qt::Key_O}),
      .action = [this] { document_.requestOpen(); },
  });
  commands_.add({
      .id = QStringLiteral("project.save"),
      .name = tr("Save Project"),
      .description = tr("Saves the project into its folder, keeping the "
                        "previous version as a backup."),
      .shortcuts = keys({ctrl | Qt::Key_S}),
      .action = [this] { document_.save(); },
  });
  commands_.add({
      .id = QStringLiteral("project.saveAs"),
      .name = tr("Save Project As…"),
      .description = tr("Saves the project into a new, empty folder and "
                        "goes on working there."),
      .shortcuts = keys({ctrl | shift | Qt::Key_S}),
      .action = [this] { document_.requestSaveAs(); },
  });
  commands_.add({
      .id = QStringLiteral("app.quit"),
      .name = tr("Quit"),
      .description = tr("Closes LINA, after offering to save the project."),
      .shortcuts = keys({ctrl | Qt::Key_Q}),
      .action = [this] { document_.requestQuit(); },
  });

  // --- Editing ---
  commands_.add({
      .id = QStringLiteral("edit.undo"),
      .name = document_.undoText(),
      .description = tr("Takes back the last change to the project."),
      .shortcuts = keys({ctrl | Qt::Key_Z}),
      .action = [this] { document_.undo(); },
  });
  commands_.add({
      .id = QStringLiteral("edit.redo"),
      .name = document_.redoText(),
      .description = tr("Makes the last change that was taken back again."),
      .shortcuts = keys({ctrl | shift | Qt::Key_Z, ctrl | Qt::Key_Y}),
      .action = [this] { document_.redo(); },
  });
  commands_.add({
      .id = QStringLiteral("edit.cancelDrag"),
      .name = tr("Cancel Drag"),
      .description = tr("Puts back what the drag going on has moved, as if it "
                        "had not started."),
      .shortcuts = keys({QKeyCombination(Qt::Key_Escape)}),
      .action =
          [this] {
            transport_.cancelTempoDrag();
            workspace_.cancelDividerDrag();
          },
  });
  commands_.add({
      .id = QStringLiteral("track.addAudio"),
      .name = tr("Add Audio Track"),
      .description = tr("Adds a track for audio clips at the end of the "
                        "track list."),
      .shortcuts = keys({ctrl | Qt::Key_T}),
      .action =
          [this] { addTrack(model::TrackKind::Audio, tr("Add Audio Track")); },
  });
  commands_.add({
      .id = QStringLiteral("track.addMidi"),
      .name = tr("Add MIDI Track"),
      .description = tr("Adds a track for MIDI clips at the end of the "
                        "track list."),
      .shortcuts = keys({ctrl | shift | Qt::Key_T}),
      .action =
          [this] { addTrack(model::TrackKind::Midi, tr("Add MIDI Track")); },
  });

  // --- Transport ---
  commands_.add({
      .id = QStringLiteral("transport.toggle"),
      .name = tr("Play or Stop"),
      .description = tr("Stops while playing; while stopped, plays from the "
                        "start marker."),
      .shortcuts = keys({QKeyCombination(Qt::Key_Space)}),
      .action = [this] { transport_.togglePlayback(); },
  });
  commands_.add({
      .id = QStringLiteral("transport.play"),
      .name = tr("Play from Start Marker"),
      .description = tr("Starts playing from the start marker."),
      .shortcuts = {},
      .action = [this] { transport_.play(); },
  });
  commands_.add({
      .id = QStringLiteral("transport.continue"),
      .name = tr("Continue from Playhead"),
      .description = tr("Starts playing from where the playhead stopped."),
      .shortcuts = keys({shift | Qt::Key_Space}),
      .action = [this] { transport_.continuePlayback(); },
  });
  commands_.add({
      .id = QStringLiteral("transport.stop"),
      .name = tr("Stop"),
      .description = tr("Stops playing and leaves the playhead where it is; "
                        "while stopped, returns it to the start marker."),
      .shortcuts = {},
      .action = [this] { transport_.stop(); },
  });
  commands_.add({
      .id = QStringLiteral("transport.tempo"),
      .name = tr("Set Tempo…"),
      .description = tr("Moves the keys to the tempo field to type a new "
                        "tempo."),
      .shortcuts = {},
      .action = [this] { Q_EMIT tempoEntryRequested(); },
  });

  // --- Views ---
  commands_.add({
      .id = QStringLiteral("view.nextPane"),
      .name = tr("Next Pane"),
      .description = tr("Gives the keys to the next view."),
      .shortcuts = keys({QKeyCombination(Qt::Key_F6)}),
      .action = [this] { workspace_.focusNextPane(); },
  });
  commands_.add({
      .id = QStringLiteral("view.previousPane"),
      .name = tr("Previous Pane"),
      .description = tr("Gives the keys to the previous view."),
      .shortcuts = keys({shift | Qt::Key_F6}),
      .action = [this] { workspace_.focusPreviousPane(); },
  });
  commands_.add({
      .id = QStringLiteral("view.session"),
      .name = tr("Go to Session"),
      .description = tr("Gives the keys to the session, showing it if it is "
                        "hidden."),
      .shortcuts = keys({ctrl | Qt::Key_1}),
      .action = [this] { workspace_.setFocusedPane(Workspace::Pane::Session); },
  });
  commands_.add({
      .id = QStringLiteral("view.arrangement"),
      .name = tr("Go to Arrangement"),
      .description = tr("Gives the keys to the arrangement, showing it if "
                        "it is hidden."),
      .shortcuts = keys({ctrl | Qt::Key_2}),
      .action =
          [this] { workspace_.setFocusedPane(Workspace::Pane::Arrangement); },
  });
  commands_.add({
      .id = QStringLiteral("view.both"),
      .name = tr("Show Session and Arrangement"),
      .description = tr("Shows both views, one above the other."),
      .shortcuts = {},
      .action = [this] { workspace_.setViews(Workspace::Views::Both); },
  });
  commands_.add({
      .id = QStringLiteral("view.sessionOnly"),
      .name = tr("Show Session Only"),
      .description = tr("Lets the session use the whole area."),
      .shortcuts = {},
      .action = [this] { workspace_.setViews(Workspace::Views::SessionOnly); },
  });
  commands_.add({
      .id = QStringLiteral("view.arrangementOnly"),
      .name = tr("Show Arrangement Only"),
      .description = tr("Lets the arrangement use the whole area."),
      .shortcuts = {},
      .action =
          [this] { workspace_.setViews(Workspace::Views::ArrangementOnly); },
  });
  commands_.add({
      .id = QStringLiteral("view.dividerUp"),
      .name = tr("Give Arrangement More Room"),
      .description = tr("Moves the divider between the views up."),
      .shortcuts = {},
      .action = [this] { workspace_.moveDivider(-1); },
  });
  commands_.add({
      .id = QStringLiteral("view.dividerDown"),
      .name = tr("Give Session More Room"),
      .description = tr("Moves the divider between the views down."),
      .shortcuts = {},
      .action = [this] { workspace_.moveDivider(1); },
  });

  // --- Appearance ---
  commands_.add({
      .id = QStringLiteral("view.larger"),
      .name = tr("Make Everything Larger"),
      .description = tr("Makes text and controls larger."),
      .shortcuts = keys({ctrl | Qt::Key_Plus, ctrl | Qt::Key_Equal}),
      .action = [this] { appearance_.larger(); },
  });
  commands_.add({
      .id = QStringLiteral("view.smaller"),
      .name = tr("Make Everything Smaller"),
      .description = tr("Makes text and controls smaller."),
      .shortcuts = keys({ctrl | Qt::Key_Minus}),
      .action = [this] { appearance_.smaller(); },
  });
  commands_.add({
      .id = QStringLiteral("view.actualSize"),
      .name = tr("Usual Size"),
      .description = tr("Shows text and controls at the desktop's own size."),
      .shortcuts = keys({ctrl | Qt::Key_0}),
      .action = [this] { appearance_.resetScale(); },
  });
  commands_.add({
      .id = QStringLiteral("view.followDesktop"),
      .name = tr("Appearance: Follow the Desktop"),
      .description = tr("Uses light or dark colours as the desktop does."),
      .shortcuts = {},
      .action =
          [this] { appearance_.setMode(Appearance::Mode::FollowDesktop); },
  });
  commands_.add({
      .id = QStringLiteral("view.light"),
      .name = tr("Appearance: Light"),
      .description = tr("Uses light colours whatever the desktop does."),
      .shortcuts = {},
      .action = [this] { appearance_.setMode(Appearance::Mode::Light); },
  });
  commands_.add({
      .id = QStringLiteral("view.dark"),
      .name = tr("Appearance: Dark"),
      .description = tr("Uses dark colours whatever the desktop does."),
      .shortcuts = {},
      .action = [this] { appearance_.setMode(Appearance::Mode::Dark); },
  });
}

void Shell::refreshCommands() {
  commands_.setName(QStringLiteral("edit.undo"), document_.undoText());
  commands_.setName(QStringLiteral("edit.redo"), document_.redoText());
  commands_.setEnabled(QStringLiteral("edit.undo"), document_.canUndo());
  commands_.setEnabled(QStringLiteral("edit.redo"), document_.canRedo());

  // The transport commands stay enabled without an output: playing then
  // says why it cannot (TransportController::play).
  commands_.setEnabled(QStringLiteral("edit.cancelDrag"),
                       transport_.tempoDragging() ||
                           workspace_.dividerDragging());

  const bool both = workspace_.views() == Workspace::Views::Both;
  commands_.setEnabled(QStringLiteral("view.dividerUp"), both);
  commands_.setEnabled(QStringLiteral("view.dividerDown"), both);
  commands_.setEnabled(QStringLiteral("view.both"), !both);
  commands_.setEnabled(QStringLiteral("view.sessionOnly"),
                       workspace_.views() != Workspace::Views::SessionOnly);
  commands_.setEnabled(QStringLiteral("view.arrangementOnly"),
                       workspace_.views() != Workspace::Views::ArrangementOnly);

  commands_.setEnabled(QStringLiteral("view.larger"), appearance_.canGrow());
  commands_.setEnabled(QStringLiteral("view.smaller"), appearance_.canShrink());
  const auto mode = appearance_.mode();
  commands_.setEnabled(QStringLiteral("view.followDesktop"),
                       mode != Appearance::Mode::FollowDesktop);
  commands_.setEnabled(QStringLiteral("view.light"),
                       mode != Appearance::Mode::Light);
  commands_.setEnabled(QStringLiteral("view.dark"),
                       mode != Appearance::Mode::Dark);
}

} // namespace lina::ui
