// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_SHELL_HPP
#define LINA_UI_SHELL_HPP

#include "lina/bridge/playback.hpp"
#include "lina/format/file_system.hpp"
#include "lina/ui/appearance.hpp"
#include "lina/ui/command_palette.hpp"
#include "lina/ui/command_registry.hpp"
#include "lina/ui/document_controller.hpp"
#include "lina/ui/track_list_model.hpp"
#include "lina/ui/transport_controller.hpp"
#include "lina/ui/workspace.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace lina::ui {

/// Everything the main window shows and drives, as one object handed to the
/// QML: the open document, the transport, the commands and their list, the
/// track headers' model, the layout and the appearance. It registers every
/// command once and keeps their enabled state and names up to date.
///
/// The window is given the shell as a required property; nothing is reached
/// through globals.
class Shell final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the application")
  Q_PROPERTY(
      lina::ui::DocumentController *document READ document CONSTANT FINAL)
  Q_PROPERTY(
      lina::ui::TransportController *transport READ transport CONSTANT FINAL)
  Q_PROPERTY(lina::ui::CommandRegistry *commands READ commands CONSTANT FINAL)
  Q_PROPERTY(lina::ui::CommandPalette *palette READ palette CONSTANT FINAL)
  Q_PROPERTY(lina::ui::TrackListModel *tracks READ tracks CONSTANT FINAL)
  Q_PROPERTY(lina::ui::Workspace *workspace READ workspace CONSTANT FINAL)
  Q_PROPERTY(lina::ui::Appearance *appearance READ appearance CONSTANT FINAL)
  Q_PROPERTY(
      QString windowTitle READ windowTitle NOTIFY windowTitleChanged FINAL)

public:
  /// `playback` and `files` must outlive the shell. `writtenBy` names the
  /// application and its version.
  Shell(bridge::Playback &playback, format::FileSystem &files,
        const QString &writtenBy, QObject *parent = nullptr);

  [[nodiscard]] DocumentController *document() { return &document_; }
  [[nodiscard]] TransportController *transport() { return &transport_; }
  [[nodiscard]] CommandRegistry *commands() { return &commands_; }
  [[nodiscard]] CommandPalette *palette() { return &palette_; }
  [[nodiscard]] TrackListModel *tracks() { return &tracks_; }
  [[nodiscard]] Workspace *workspace() { return &workspace_; }
  [[nodiscard]] Appearance *appearance() { return &appearance_; }

  /// "Song – LINA", with a mark while there are unsaved changes.
  [[nodiscard]] QString windowTitle() const;

  /// Runs a command by id; for the QML and for tests.
  Q_INVOKABLE bool run(const QString &id);

Q_SIGNALS:
  void windowTitleChanged();
  /// Commands that the window carries out itself.
  void commandListRequested();
  void tempoEntryRequested();

private:
  void registerCommands();
  void refreshCommands();
  void addTrack(model::TrackKind kind, const QString &name);

  // Declared in the order they depend on each other.
  DocumentController document_;
  TransportController transport_;
  CommandRegistry commands_;
  CommandPalette palette_;
  TrackListModel tracks_;
  Workspace workspace_;
  Appearance appearance_;
};

} // namespace lina::ui

#endif // LINA_UI_SHELL_HPP
