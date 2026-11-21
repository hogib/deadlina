// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project folder (docs/design/10 and 16): where each file lives, saving
// without ever leaving a half-written project, the backup, the recovery copy,
// the lock, and the stores for plugin state and media.
//
//   My Song/
//     project.lina          the document
//     view.lina             view state
//     media/                imported copies and recordings
//     state/                plugin state, one file per distinct content
//     backup/project.lina   the previous saved document
//     recovery/project.lina present only while there are unsaved changes
//     cache/                safe to delete
//     .lock                 present while the project is open
//
// Nothing here reads a clock or starts a thread, and no function throws.
// Every function may be called from a worker thread; one folder must not be
// used from two threads at once.
//
// Files inside the folder that LINA reads or replaces must be regular files:
// a symbolic link in the place of project.lina, view.lina, a backup, the
// recovery copy or a state file is refused (`NotAFile`), never followed and
// never silently replaced.
#ifndef LINA_FORMAT_PROJECT_FOLDER_HPP
#define LINA_FORMAT_PROJECT_FOLDER_HPP

#include "lina/format/document_io.hpp"
#include "lina/format/error.hpp"
#include "lina/format/file_system.hpp"
#include "lina/format/view_state.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lina::format {

// The largest plugin state `loadState` hands back in one piece: 1 GiB.
inline constexpr std::uint64_t maxStateBytes = std::uint64_t{1} << 30U;

// What an instance writes into the lock file about itself. It is there for
// people to read ("open in LINA 0.1.0, process 4242 on studio"); it decides
// nothing, since process numbers and host names are not unique across
// containers and machines. The lock itself is held by the kernel.
struct LockNote {
  std::int64_t processId{0};
  std::string hostName;
  std::string application;

  friend bool operator==(const LockNote &, const LockNote &) = default;
};

// The note of the running process.
LockNote currentProcessNote(std::string application);

struct ProjectLock {
  LockState state{LockState::Unsupported};
  // The lock, for `Acquired`: keep it for as long as the project is open.
  // Letting go of it removes the lock file.
  std::unique_ptr<FileLock> held;
  // Who holds the lock, for `HeldElsewhere`, when the lock file says so
  // legibly.
  std::optional<LockNote> holder;
};

// What `save` did beyond putting the new document in place.
struct SaveReport {
  // The name inside backup/ under which the document that was replaced is
  // kept:
  //   "project.lina"            the usual backup, replaced by every save;
  //   "project.v<N>.lina"       it was of the older format version N: kept
  //                             apart and never replaced by later saves;
  //   "project.unreadable.lina" its format version could not be made out:
  //                             kept apart, so that it does not take the
  //                             place of an older backup that is good.
  // Empty for the first save of a project, and when the backup could not be
  // put in place (see `afterSwapProblems`).
  std::string backupName;
  // What the saved document refers to inside the folder and was not there
  // when it was saved, sorted: names of state files, and paths of media
  // files relative to the folder. The save went through all the same, since
  // a project with missing files stays usable (doc 10); the application
  // tells the user.
  std::vector<std::string> missingStates;
  std::vector<std::string> missingMedia;
  // Failures after the new document was safely in place: the backup could
  // not be given its final name (the previous backup then stays), or the
  // recovery copy could not be removed. The save itself stands.
  std::vector<Error> afterSwapProblems;
};

// A recovery copy that differs from the saved document.
struct RecoveryInfo {
  // When it was written, by the file system's clock: to show, not to decide
  // by.
  FileTime written;
};

// A file copied into media/, as a media reference needs it.
struct ImportedMedia {
  // Relative to the project folder: "media/<name>-<8 hex digits><extension>".
  std::string path;
  std::uint64_t sizeBytes{0};
  // SHA-256 of the content, 64 lower-case hexadecimal digits.
  std::string contentHash;
};

// The name a file gets inside media/: its own name with the first eight digits
// of its content hash before the extension.
std::string mediaFileName(const std::filesystem::path &original,
                          std::string_view contentHash);

// True for the name of a state file: 64 lower-case hexadecimal digits followed
// by ".bin".
bool isStateFileName(std::string_view name);

class ProjectFolder {
public:
  // `fileSystem` must outlive the folder object.
  ProjectFolder(FileSystem &fileSystem, std::filesystem::path root);

  // --- Layout ---

  [[nodiscard]] const std::filesystem::path &root() const { return root_; }
  [[nodiscard]] std::filesystem::path documentPath() const;
  [[nodiscard]] std::filesystem::path viewPath() const;
  [[nodiscard]] std::filesystem::path mediaFolder() const;
  [[nodiscard]] std::filesystem::path stateFolder() const;
  [[nodiscard]] std::filesystem::path backupFolder() const;
  [[nodiscard]] std::filesystem::path recoveryFolder() const;
  [[nodiscard]] std::filesystem::path recoveryDocumentPath() const;
  [[nodiscard]] std::filesystem::path cacheFolder() const;
  [[nodiscard]] std::filesystem::path lockPath() const;

  // Makes the folder with its media/, state/ and cache/ folders. Succeeds when
  // they are there already. The parent of the folder must exist.
  std::expected<void, Error> create();

  // Removes temporary files that an interrupted save, import or store left
  // behind, and returns their paths relative to the folder. Only names this
  // library itself creates are touched. To be called once after the lock has
  // been acquired, before anything else writes.
  std::expected<std::vector<std::string>, Error> removeStaleTemporaries();

  // --- The document ---

  std::expected<LoadedDocument, Error> load();

  // Puts a new document in place:
  //   1. (before the call) new state and media files are stored through
  //      `storeState` and `importMedia`, under names derived from content;
  //   2. every state file the document refers to, and every media file it
  //      says is inside the folder, is looked for; the ones that are not
  //      there are listed in the report, and the save goes on;
  //   3. the current document is copied to a temporary file in backup/ and
  //      flushed;
  //   4. the new document is written to project.lina.tmp and flushed, with
  //      the permissions of the document it replaces;
  //   5. project.lina.tmp is renamed over project.lina and the folder is
  //      flushed: once both have succeeded the save stands;
  //   6. the backup's temporary file is renamed to its final name (see
  //      `SaveReport::backupName`), replacing the previous backup only now;
  //   7. the recovery copy is removed.
  // A failure up to the rename in step 5 returns an error, removes both
  // temporary files and leaves project.lina and the previous backup exactly
  // as they were. If the flush in step 5 fails, an error is returned although
  // project.lina already holds the new document: whether it survives a power
  // loss is then not known. project.lina is a complete document at every
  // instant. State files are never deleted here: see `collectStates`.
  std::expected<SaveReport, Error> save(const DocumentImage &image);

  // Deletes the state files nothing refers to any more and returns their
  // names. Kept are: every name in `keep`, which must hold all that the open
  // session can still reach (the document in memory, its undo and redo
  // history, a save that is queued); and whatever project.lina, every
  // document in backup/ and the recovery copy refer to. If one of those files
  // is there but cannot be read, nothing is deleted and the error says which.
  // Only names of state files are ever deleted.
  std::expected<std::vector<std::string>, Error>
  collectStates(std::span<const std::string> keep);

  // --- Recovery copy ---

  // Writes recovery/project.lina: temporary file, flush, rename.
  std::expected<void, Error> writeRecovery(const DocumentImage &image);
  // The recovery copy to offer: one that exists and whose bytes differ from
  // project.lina (or there is no project.lina). `save` removes the copy, so
  // one that is there was written after the last save.
  std::expected<std::optional<RecoveryInfo>, Error> findRecovery();
  std::expected<LoadedDocument, Error> loadRecovery();
  // Removes recovery/. `save` does this too.
  std::expected<void, Error> discardRecovery();

  // --- Plugin state ---

  // Stores opaque bytes in state/ under the name "<SHA-256>.bin" and returns
  // that name, which is what a device's state reference holds. A file of
  // that name that already holds the content is left alone; one that does
  // not is replaced.
  std::expected<std::string, Error> storeState(std::string_view bytes);
  // `Corrupt` when the file does not hash to its name; `TooLarge` past
  // `maxStateBytes`.
  std::expected<std::string, Error> loadState(std::string_view name);

  // --- Media ---

  // Copies a file into media/, in pieces, and says how to refer to it.
  // Content that is in media/ already, under whatever name, is found instead
  // of stored twice. The source may be a symbolic link.
  std::expected<ImportedMedia, Error>
  importMedia(const std::filesystem::path &source);
  // Where the file of a media reference is: inside the folder for a relative
  // path, the path itself for an absolute one. Empty for a path a document
  // may not hold.
  [[nodiscard]] std::optional<std::filesystem::path>
  mediaLocation(std::string_view documentPath) const;

  // --- View state ---

  std::expected<void, Error> saveView(const ViewState &state);
  std::expected<ViewState, Error> loadView();

  // --- Lock ---

  // Tries to take the lock of the folder for this instance, without waiting.
  //   `Acquired`       this instance has the project, until `held` goes.
  //   `HeldElsewhere`  another running instance has it; `holder` says who,
  //                    if that could be read. Warn the user.
  //   `Unsupported`    the file system has no locks. Nothing is held.
  // Fails with `ReadOnly` when the folder cannot be written to, so that the
  // application can offer to open the project without saving.
  std::expected<ProjectLock, Error> acquireLock(const LockNote &note);

private:
  FileSystem *fileSystem_;
  std::filesystem::path root_;
};

} // namespace lina::format

#endif // LINA_FORMAT_PROJECT_FOLDER_HPP
