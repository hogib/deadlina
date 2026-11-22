// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_DOCUMENT_CONTROLLER_HPP
#define LINA_UI_DOCUMENT_CONTROLLER_HPP

#include "lina/bridge/playback.hpp"
#include "lina/format/file_system.hpp"
#include "lina/format/project_folder.hpp"
#include "lina/format/view_state.hpp"
#include "lina/model/command.hpp"
#include "lina/model/document.hpp"
#include "lina/ui/prompt.hpp"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace lina::ui {

/// Owns the open project: the document, its undo history, its folder and
/// the folder's lock.
///
/// Every change goes through a command applied to the document
/// (docs/design/12-interaction-principles.md, principle 3). Each change set
/// the document returns is handed to the playback engine and then announced
/// with projectChanged(), in that order, so that thin models over the
/// document (TrackListModel) see every edit
/// (docs/design/13-architecture.md, "The interface", item 6).
///
/// Files: new, open, save and save as, through the project folder of the
/// format library. A project that is open elsewhere is reported and not
/// opened; a recovery copy is offered when there is one; a folder without
/// locks or one that cannot be written to is opened only if the user says
/// so. Questions go through `prompt`; reports that need no answer go to
/// `notice`. Nothing here opens a file dialog: when a folder is needed,
/// folderWanted() asks the interface for one, and the answer comes back
/// through folderChosen() or folderChoiceCancelled().
///
/// Saving, loading and the lock run on the main thread for now
/// (docs/design/13-architecture.md, "Open questions").
class DocumentController final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the shell")
  Q_PROPERTY(QString title READ title NOTIFY locationChanged FINAL)
  Q_PROPERTY(QString folderPath READ folderPath NOTIFY locationChanged FINAL)
  Q_PROPERTY(bool readOnly READ readOnly NOTIFY locationChanged FINAL)
  Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged FINAL)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged FINAL)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged FINAL)
  Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged FINAL)
  Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged FINAL)
  Q_PROPERTY(lina::ui::Prompt *prompt READ prompt CONSTANT FINAL)
  Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged FINAL)
  Q_PROPERTY(bool noticeIsError READ noticeIsError NOTIFY noticeChanged FINAL)

public:
  enum class FolderPurpose : std::uint8_t { Open, SaveAs };
  Q_ENUM(FolderPurpose)

  /// `writtenBy` names the application and its version ("LINA 0.0.1"): it
  /// goes into saved documents and the lock file. The playback engine and
  /// the file system must outlive the controller. Starts with an empty,
  /// untitled project, which the engine is given at once.
  DocumentController(bridge::Playback &playback, format::FileSystem &files,
                     QString writtenBy, QObject *parent = nullptr);
  ~DocumentController() override;

  DocumentController(const DocumentController &) = delete;
  DocumentController &operator=(const DocumentController &) = delete;
  DocumentController(DocumentController &&) = delete;
  DocumentController &operator=(DocumentController &&) = delete;

  [[nodiscard]] const model::Project &project() const {
    return document_.project();
  }

  // --- Editing ---

  /// Applies commands as one undo step named `name` (translated, shown as
  /// "Undo <name>"). On failure nothing changes and the notice says why.
  bool perform(const QString &name, std::span<const model::Command> commands);
  bool perform(const QString &name, const model::Command &command);

  /// A gesture that applies commands as it goes (a drag) and is one undo
  /// step at its end (principle 3). Each command reaches the engine and the
  /// models at once. Undo and redo wait until it ends.
  bool beginGesture(const QString &name);
  bool applyInGesture(const model::Command &command);
  void endGesture();
  /// Takes back everything the gesture applied (Escape during a drag).
  void cancelGesture();
  [[nodiscard]] bool inGesture() const { return document_.inTransaction(); }

  /// Refused, with nothing happening, while a gesture is going on.
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  [[nodiscard]] QString undoText() const;
  [[nodiscard]] QString redoText() const;

  // --- The project and its folder ---

  [[nodiscard]] QString title() const;
  [[nodiscard]] QString folderPath() const;
  [[nodiscard]] bool readOnly() const;
  [[nodiscard]] bool modified() const;
  [[nodiscard]] std::optional<std::filesystem::path> folder() const;

  /// Each of these first finishes a gesture that is going on, then offers to
  /// save unsaved changes. While a question waits they do nothing.
  Q_INVOKABLE void requestNew();
  Q_INVOKABLE void requestOpen();
  Q_INVOKABLE void requestQuit();

  /// Saves into the project's folder, or asks for one. A gesture that is
  /// going on is finished first and saved with the rest.
  Q_INVOKABLE bool save();
  Q_INVOKABLE void requestSaveAs();

  /// The answer to folderWanted().
  Q_INVOKABLE void folderChosen(const QUrl &folder);
  Q_INVOKABLE void folderChoiceCancelled();

  /// Opens the project in `root` without asking about unsaved changes:
  /// for the command line, and after requestOpen(). Does nothing while a
  /// question waits; says so when `root` is the project open already.
  void openFolder(const std::filesystem::path &root);
  /// Saves into `root`, which must be empty or not exist yet (its parent
  /// must), and makes it the project's folder.
  bool saveAsFolder(const std::filesystem::path &root);

  // --- View state and reports ---

  /// What the user was looking at, saved beside the document.
  [[nodiscard]] const format::ViewState &viewState() const {
    return viewState_;
  }
  /// Where the view state comes from when the project is saved: given the
  /// one loaded or saved last, it returns what the window shows now.
  void
  setViewSource(std::function<format::ViewState(format::ViewState)> source) {
    viewSource_ = std::move(source);
  }

  [[nodiscard]] Prompt *prompt() { return &prompt_; }
  [[nodiscard]] QString notice() const { return notice_; }
  [[nodiscard]] bool noticeIsError() const { return noticeIsError_; }
  /// Shows a report that needs no answer, replacing the one shown.
  void notify(const QString &text, bool error);
  Q_INVOKABLE void dismissNotice();

Q_SIGNALS:
  /// Before another project takes the place of the open one.
  void projectAboutToReset();
  /// After another project took its place; models rebuild.
  void projectReset();
  /// After an edit, an undo or a redo, with what it changed. The engine has
  /// already been given the change set.
  void projectChanged(const lina::model::ChangeSet &changes);
  void historyChanged();
  void modifiedChanged();
  void locationChanged();
  void noticeChanged();
  /// The interface is to ask the user for a folder.
  void folderWanted(lina::ui::DocumentController::FolderPurpose purpose);
  /// Unsaved changes are saved or given up: the application may end.
  void quitApproved();
  /// An opened project brought its view state.
  void viewStateLoaded();

private:
  struct OpenFolder {
    std::filesystem::path root;
    /// Shared only so that it can wait inside a continuation while a
    /// question is asked; one owner at any time.
    std::shared_ptr<format::FileLock> lock;
    bool readOnly{false};
  };

  void dispatch(const model::ChangeSet &changes);
  void pushStep();
  void failed(const QString &name, const model::Failure &failure);
  void announceHistory(bool modifiedBefore);

  /// Runs `then` when there are no unsaved changes, or after the user
  /// saved or gave them up.
  void guardUnsaved(std::function<void()> then);
  void wantFolder(FolderPurpose purpose);

  [[nodiscard]] bool isOpenFolder(const std::filesystem::path &root) const;
  void reportHeldElsewhere(const QString &name,
                           const std::optional<format::LockNote> &holder);
  void askToOpenUnprotected(const std::filesystem::path &root);
  void continueOpen(const std::filesystem::path &root,
                    std::shared_ptr<format::FileLock> lock, bool readOnly);
  void finishOpen(const OpenFolder &opened, bool fromRecovery);
  void install(model::Document document, std::optional<OpenFolder> folder,
               bool fromRecovery);
  bool saveInto(const OpenFolder &target);

  bridge::Playback *playback_;
  format::FileSystem *files_;
  QString writtenBy_;
  model::Document document_;
  std::optional<OpenFolder> folder_;
  format::ViewState viewState_;
  std::function<format::ViewState(format::ViewState)> viewSource_;

  /// Mirrors the document's undo and redo history with a number per step,
  /// so that "modified" is exact: the project is modified when the newest
  /// undo step is not the one that was saved.
  std::vector<std::uint64_t> undoSteps_;
  std::vector<std::uint64_t> redoSteps_;
  std::uint64_t nextStep_{1};
  std::uint64_t savedStep_{0};
  model::ChangeSet gestureChanges_;

  Prompt prompt_;
  QString notice_;
  bool noticeIsError_{false};
  std::optional<FolderPurpose> folderPurpose_;
  /// What waits for a save that needs a folder first.
  std::function<void()> afterSave_;
};

} // namespace lina::ui

#endif // LINA_UI_DOCUMENT_CONTROLLER_HPP
