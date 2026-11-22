// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/document_controller.hpp"

#include "lina/format/document_io.hpp"
#include "lina/format/project_folder.hpp"
#include "lina/ui/fault_text.hpp"

#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

namespace lina::ui {

namespace {

/// The step number of a project that differs from what is saved before any
/// edit: one opened from its recovery copy.
constexpr std::uint64_t neverSaved = std::numeric_limits<std::uint64_t>::max();

QString fromPath(const std::filesystem::path &path) {
  return QString::fromStdString(path.string());
}

std::filesystem::path toPath(const QUrl &url) {
  return {url.toLocalFile().toStdString()};
}

QString explain(const format::Error &error) {
  return QString::fromStdString(format::describe(error));
}

/// Where relative media paths are looked for while a project has no
/// folder. An untitled project cannot hold any, since media are copied
/// into the folder; the engine wants an absolute path all the same.
std::filesystem::path untitledMediaFolder() {
  return QDir::tempPath().toStdString();
}

/// Whether a project refers to files inside its folder: media with a
/// relative path, or plugin state.
bool keepsFilesInFolder(const model::Project &project) {
  const auto relative = [](const model::MediaReference &media) {
    return !std::filesystem::path(media.path).is_absolute();
  };
  const auto stateful = [](const model::Track &track) {
    return std::ranges::any_of(track.devices, [](const model::Device &device) {
      return !device.stateRef.empty();
    });
  };
  return std::ranges::any_of(project.media, relative) ||
         std::ranges::any_of(project.tracks, stateful) ||
         std::ranges::any_of(project.returns, stateful) ||
         stateful(project.master);
}

QString writtenAt(format::FileTime time) {
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          time.time_since_epoch())
          .count();
  return QLocale().toString(QDateTime::fromMSecsSinceEpoch(milliseconds),
                            QLocale::LongFormat);
}

} // namespace

DocumentController::DocumentController(bridge::Playback &playback,
                                       format::FileSystem &files,
                                       QString writtenBy, QObject *parent)
    : QObject(parent), playback_(&playback), files_(&files),
      writtenBy_(std::move(writtenBy)) {
  playback_->setProject(document_.project(), untitledMediaFolder());
}

DocumentController::~DocumentController() = default;

// --- Editing ---

bool DocumentController::perform(const QString &name,
                                 std::span<const model::Command> commands) {
  if (inGesture()) {
    return false;
  }
  const bool modifiedBefore = modified();
  auto changes = document_.perform(name.toStdString(), commands);
  if (!changes) {
    failed(name, changes.error());
    return false;
  }
  if (!changes->empty()) {
    pushStep();
    dispatch(*changes);
  }
  announceHistory(modifiedBefore);
  return true;
}

bool DocumentController::perform(const QString &name,
                                 const model::Command &command) {
  return perform(name, std::span{&command, 1});
}

bool DocumentController::beginGesture(const QString &name) {
  if (!document_.begin(name.toStdString())) {
    return false;
  }
  gestureChanges_ = {};
  // Undo and redo wait until the gesture ends.
  Q_EMIT historyChanged();
  return true;
}

bool DocumentController::applyInGesture(const model::Command &command) {
  if (!inGesture()) {
    return false;
  }
  auto changes = document_.apply(command);
  if (!changes) {
    return false;
  }
  const bool modifiedBefore = modified();
  gestureChanges_.merge(*changes);
  if (!changes->empty()) {
    dispatch(*changes);
  }
  if (modified() != modifiedBefore) {
    Q_EMIT modifiedChanged();
  }
  return true;
}

void DocumentController::endGesture() {
  if (!inGesture()) {
    return;
  }
  const bool modifiedBefore = modified();
  // A commit of a transaction that is open cannot fail.
  std::ignore = document_.commit();
  // The document keeps a step exactly when the merged changes are not
  // empty (document.hpp), and so does the mirror.
  if (!gestureChanges_.empty()) {
    pushStep();
  }
  gestureChanges_ = {};
  announceHistory(modifiedBefore);
}

void DocumentController::cancelGesture() {
  if (!inGesture()) {
    return;
  }
  const bool modifiedBefore = modified();
  // Taking back an open transaction cannot fail.
  const model::ChangeSet changes =
      document_.rollback().value_or(model::ChangeSet{});
  gestureChanges_ = {};
  if (!changes.empty()) {
    dispatch(changes);
  }
  announceHistory(modifiedBefore);
}

void DocumentController::undo() {
  const bool modifiedBefore = modified();
  // Refused with nothing to undo, and during a gesture.
  auto changes = document_.undo();
  if (!changes) {
    return;
  }
  redoSteps_.push_back(undoSteps_.back());
  undoSteps_.pop_back();
  dispatch(*changes);
  announceHistory(modifiedBefore);
}

void DocumentController::redo() {
  const bool modifiedBefore = modified();
  auto changes = document_.redo();
  if (!changes) {
    return;
  }
  undoSteps_.push_back(redoSteps_.back());
  redoSteps_.pop_back();
  dispatch(*changes);
  announceHistory(modifiedBefore);
}

bool DocumentController::canUndo() const {
  return document_.canUndo() && !inGesture();
}

bool DocumentController::canRedo() const {
  return document_.canRedo() && !inGesture();
}

QString DocumentController::undoText() const {
  const auto name = document_.undoName();
  return name ? tr("Undo %1").arg(QString::fromStdString(std::string(*name)))
              : tr("Undo");
}

QString DocumentController::redoText() const {
  const auto name = document_.redoName();
  return name ? tr("Redo %1").arg(QString::fromStdString(std::string(*name)))
              : tr("Redo");
}

void DocumentController::dispatch(const model::ChangeSet &changes) {
  try {
    playback_->applyChanges(document_.project(), changes);
  } catch (const std::exception &error) {
    // The engine has rebuilt its edit from the project, or has none
    // (engine.hpp); the document is right either way.
    notify(tr("Playback could not follow the last change (%1). Save your "
              "work and open the project again to hear it as it is.")
               .arg(QString::fromUtf8(error.what())),
           true);
  }
  Q_EMIT projectChanged(changes);
}

void DocumentController::pushStep() {
  undoSteps_.push_back(nextStep_++);
  redoSteps_.clear();
}

void DocumentController::failed(const QString &name,
                                const model::Failure &failure) {
  notify(tr("“%1” could not be done: %2. Nothing was changed.")
             .arg(name, faultText(failure.fault)),
         true);
}

void DocumentController::announceHistory(bool modifiedBefore) {
  Q_EMIT historyChanged();
  if (modified() != modifiedBefore) {
    Q_EMIT modifiedChanged();
  }
}

// --- The project and its folder ---

QString DocumentController::title() const {
  if (!folder_) {
    return tr("Untitled");
  }
  return fromPath(folder_->root.filename());
}

QString DocumentController::folderPath() const {
  return folder_ ? fromPath(folder_->root) : QString();
}

bool DocumentController::readOnly() const {
  return folder_ && folder_->readOnly;
}

bool DocumentController::modified() const {
  const std::uint64_t current = undoSteps_.empty() ? 0 : undoSteps_.back();
  return current != savedStep_ || !gestureChanges_.empty();
}

bool DocumentController::isOpenFolder(const std::filesystem::path &root) const {
  if (!folder_) {
    return false;
  }
  std::error_code error;
  return std::filesystem::equivalent(root, folder_->root, error);
}

std::optional<std::filesystem::path> DocumentController::folder() const {
  if (!folder_) {
    return std::nullopt;
  }
  return folder_->root;
}

void DocumentController::guardUnsaved(std::function<void()> then) {
  // One question at a time: a request that comes while one waits (the
  // window's close button while "Save changes?" is up) is dropped.
  if (prompt_.active()) {
    return;
  }
  // A gesture still going on is finished, so that what it did is saved or
  // given up with the rest.
  endGesture();
  if (!modified()) {
    then();
    return;
  }
  constexpr int saveChoice = 0;
  constexpr int discardChoice = 1;
  constexpr int cancelChoice = 2;
  prompt_.ask(
      tr("Save changes to “%1”?").arg(title()),
      tr("If you do not save, the changes made since the last save are lost. "
         "This cannot be undone."),
      {tr("Save"), tr("Do Not Save"), tr("Cancel")}, cancelChoice,
      [this, then = std::move(then)](int choice) {
        if (choice == discardChoice) {
          then();
        } else if (choice == saveChoice) {
          if (folder_ && !folder_->readOnly) {
            if (save()) {
              then();
            }
          } else {
            afterSave_ = then;
            wantFolder(FolderPurpose::SaveAs);
          }
        }
      });
}

void DocumentController::wantFolder(FolderPurpose purpose) {
  folderPurpose_ = purpose;
  Q_EMIT folderWanted(purpose);
}

void DocumentController::requestNew() {
  guardUnsaved([this] { install(model::Document{}, std::nullopt, false); });
}

void DocumentController::requestOpen() {
  guardUnsaved([this] { wantFolder(FolderPurpose::Open); });
}

void DocumentController::requestQuit() {
  guardUnsaved([this] { Q_EMIT quitApproved(); });
}

void DocumentController::requestSaveAs() {
  // Started by the user, not by a question: nothing waits for it.
  afterSave_ = nullptr;
  endGesture();
  wantFolder(FolderPurpose::SaveAs);
}

bool DocumentController::save() {
  afterSave_ = nullptr;
  // What a gesture has done so far is kept and saved with the rest.
  endGesture();
  if (!folder_ || folder_->readOnly) {
    if (folder_) {
      notify(tr("This project was opened without the right to save into its "
                "folder. Choose another folder to save it in."),
             false);
    }
    wantFolder(FolderPurpose::SaveAs);
    return false;
  }
  return saveInto(*folder_);
}

void DocumentController::folderChosen(const QUrl &folder) {
  const auto purpose = std::exchange(folderPurpose_, std::nullopt);
  if (!purpose) {
    return;
  }
  const std::filesystem::path path = toPath(folder);
  if (*purpose == FolderPurpose::Open) {
    openFolder(path);
    return;
  }
  // Whatever waited for this save goes on only if the save went through;
  // otherwise it is dropped, and a later save does not set it off.
  const std::function<void()> then = std::exchange(afterSave_, nullptr);
  if (saveAsFolder(path) && then) {
    then();
  }
}

void DocumentController::folderChoiceCancelled() {
  folderPurpose_.reset();
  afterSave_ = nullptr;
}

void DocumentController::openFolder(const std::filesystem::path &root) {
  // Opening may ask a question, and only one can wait at a time.
  if (prompt_.active()) {
    return;
  }
  const QString name = fromPath(root.filename());
  if (isOpenFolder(root)) {
    notify(tr("“%1” is already open in this window.").arg(name), false);
    return;
  }
  format::ProjectFolder folder(*files_, root);
  auto lock =
      folder.acquireLock(format::currentProcessNote(writtenBy_.toStdString()));
  if (!lock) {
    if (lock.error().code != format::ErrorCode::ReadOnly) {
      notify(
          tr("“%1” could not be opened: %2.").arg(name, explain(lock.error())),
          true);
      return;
    }
    constexpr int openChoice = 0;
    constexpr int cancelChoice = 1;
    prompt_.ask(tr("“%1” cannot be saved into").arg(name),
                tr("Its folder cannot be written to. You can open the project "
                   "to look at and play it, and save it elsewhere."),
                {tr("Open Without Saving"), tr("Cancel")}, cancelChoice,
                [this, root](int choice) {
                  if (choice == openChoice) {
                    continueOpen(root, nullptr, true);
                  }
                });
    return;
  }
  if (lock->state == format::LockState::Acquired) {
    continueOpen(root, std::shared_ptr<format::FileLock>(std::move(lock->held)),
                 false);
  } else if (lock->state == format::LockState::HeldElsewhere) {
    reportHeldElsewhere(name, lock->holder);
  } else {
    askToOpenUnprotected(root);
  }
}

void DocumentController::reportHeldElsewhere(
    const QString &name, const std::optional<format::LockNote> &holder) {
  const QString who =
      holder ? tr(" (%1, process %2 on %3)")
                   .arg(QString::fromStdString(holder->application))
                   .arg(holder->processId)
                   .arg(QString::fromStdString(holder->hostName))
             : QString();
  notify(tr("“%1” is already open in another copy of LINA%2. Close it "
            "there, then open it here.")
             .arg(name, who),
         true);
}

void DocumentController::askToOpenUnprotected(
    const std::filesystem::path &root) {
  constexpr int openChoice = 0;
  constexpr int cancelChoice = 1;
  prompt_.ask(tr("“%1” cannot be protected").arg(fromPath(root.filename())),
              tr("Its folder is on a file system without locks, so another "
                 "copy of LINA could open the same project and save over "
                 "your changes."),
              {tr("Open Anyway"), tr("Cancel")}, cancelChoice,
              [this, root](int choice) {
                if (choice == openChoice) {
                  continueOpen(root, nullptr, false);
                }
              });
}

void DocumentController::continueOpen(const std::filesystem::path &root,
                                      std::shared_ptr<format::FileLock> lock,
                                      bool readOnly) {
  format::ProjectFolder folder(*files_, root);
  if (lock) {
    // Only with the lock held is no other instance writing there.
    std::ignore = folder.removeStaleTemporaries();
  }
  const OpenFolder opened{
      .root = root,
      .lock = std::move(lock),
      .readOnly = readOnly,
  };
  // A recovery copy that cannot be read is not offered.
  const std::optional<format::RecoveryInfo> recovery =
      folder.findRecovery().value_or(std::nullopt);
  if (!recovery) {
    finishOpen(opened, false);
    return;
  }
  constexpr int recoveredChoice = 0;
  constexpr int savedChoice = 1;
  prompt_.ask(
      tr("Unsaved changes to “%1” were kept").arg(fromPath(root.filename())),
      tr("LINA kept a copy of changes that were never saved, written %1. "
         "Open that copy, or the last saved version? The copy stays until "
         "the project is saved.")
          .arg(writtenAt(recovery->written)),
      {tr("Open Unsaved Changes"), tr("Open Saved Version")}, savedChoice,
      [this, opened](int choice) {
        finishOpen(opened, choice == recoveredChoice);
      });
}

void DocumentController::finishOpen(const OpenFolder &opened,
                                    bool fromRecovery) {
  format::ProjectFolder folder(*files_, opened.root);
  auto loaded = fromRecovery ? folder.loadRecovery() : folder.load();
  if (!loaded) {
    notify(tr("“%1” could not be opened: %2.")
               .arg(fromPath(opened.root.filename()), explain(loaded.error())),
           true);
    return;
  }
  // A missing or unreadable view state is no reason not to open.
  viewState_ = folder.loadView().value_or(format::ViewState{});
  install(std::move(loaded->document), opened, fromRecovery);
  Q_EMIT viewStateLoaded();
}

void DocumentController::install(model::Document document,
                                 std::optional<OpenFolder> folder,
                                 bool fromRecovery) {
  Q_EMIT projectAboutToReset();
  const bool modifiedBefore = modified();
  document_ = std::move(document);
  folder_ = std::move(folder);
  undoSteps_.clear();
  redoSteps_.clear();
  savedStep_ = fromRecovery ? neverSaved : 0;
  if (!folder_) {
    viewState_ = {};
  }
  try {
    playback_->setProject(
        document_.project(),
        folder_ ? format::ProjectFolder(*files_, folder_->root).mediaFolder()
                : untitledMediaFolder());
  } catch (const std::exception &error) {
    notify(tr("The project opened, but playback could not be prepared (%1).")
               .arg(QString::fromUtf8(error.what())),
           true);
  }
  Q_EMIT projectReset();
  Q_EMIT locationChanged();
  announceHistory(modifiedBefore);
}

bool DocumentController::saveAsFolder(const std::filesystem::path &root) {
  if (folder_ && root == folder_->root && !folder_->readOnly) {
    return saveInto(*folder_);
  }
  const QString name = fromPath(root.filename());
  if (auto names = files_->list(root); names && !names->empty()) {
    notify(tr("“%1” already holds files. Choose an empty folder, or make a "
              "new one in the folder dialog.")
               .arg(name),
           true);
    return false;
  }
  if (folder_ && keepsFilesInFolder(project())) {
    notify(tr("This project keeps media or plugin state in its folder, which "
              "Save As cannot copy yet. Save it in its own folder instead."),
           true);
    return false;
  }
  format::ProjectFolder folder(*files_, root);
  if (auto created = folder.create(); !created) {
    notify(
        tr("“%1” could not be made: %2.").arg(name, explain(created.error())),
        true);
    return false;
  }
  auto lock =
      folder.acquireLock(format::currentProcessNote(writtenBy_.toStdString()));
  if (!lock || lock->state == format::LockState::HeldElsewhere) {
    notify(tr("“%1” cannot be saved into: another copy of LINA uses it, or it "
              "cannot be written to.")
               .arg(name),
           true);
    return false;
  }
  OpenFolder opened{
      .root = root,
      .lock = std::shared_ptr<format::FileLock>(std::move(lock->held)),
      .readOnly = false,
  };
  if (!saveInto(opened)) {
    return false;
  }
  folder_ = std::move(opened);
  Q_EMIT locationChanged();
  return true;
}

bool DocumentController::saveInto(const OpenFolder &target) {
  auto image = format::writeDocument(document_, writtenBy_.toStdString());
  if (!image) {
    notify(
        tr("The project could not be saved: %1.").arg(explain(image.error())),
        true);
    return false;
  }
  format::ProjectFolder folder(*files_, target.root);
  auto report = folder.save(*image);
  if (!report) {
    notify(tr("The project could not be saved, and the last saved version is "
              "unchanged: %1. Check that the disk has room and that the folder "
              "can be written to, then save again.")
               .arg(explain(report.error())),
           true);
    return false;
  }
  const bool modifiedBefore = modified();
  savedStep_ = undoSteps_.empty() ? 0 : undoSteps_.back();
  announceHistory(modifiedBefore);

  QStringList problems;
  for (const std::string &missing : report->missingMedia) {
    problems.append(QString::fromStdString(missing));
  }
  for (const std::string &missing : report->missingStates) {
    problems.append(QString::fromStdString(missing));
  }
  if (viewSource_) {
    viewState_ = viewSource_(viewState_);
  }
  if (auto saved = folder.saveView(viewState_); !saved) {
    problems.append(tr("the view state (%1)").arg(explain(saved.error())));
  }
  for (const format::Error &error : report->afterSwapProblems) {
    problems.append(explain(error));
  }
  if (problems.isEmpty()) {
    dismissNotice();
  } else {
    notify(tr("Saved. Some files are missing or could not be tidied up: %1.")
               .arg(problems.join(QStringLiteral(", "))),
           false);
  }
  return true;
}

void DocumentController::notify(const QString &text, bool error) {
  notice_ = text;
  noticeIsError_ = error;
  Q_EMIT noticeChanged();
}

void DocumentController::dismissNotice() {
  if (notice_.isEmpty()) {
    return;
  }
  notice_.clear();
  noticeIsError_ = false;
  Q_EMIT noticeChanged();
}

} // namespace lina::ui
