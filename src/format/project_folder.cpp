// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/format/project_folder.hpp"

#include "internal/codec.hpp"
#include "internal/file_io.hpp"
#include "internal/guard.hpp"
#include "internal/save.hpp"
#include "internal/text_rules.hpp"
#include "lina/format/sha256.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <set>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace lina::format {

namespace {

constexpr std::string_view documentName = "project.lina";
constexpr std::string_view viewName = "view.lina";
constexpr std::string_view documentExtension = ".lina";
constexpr std::string_view unreadableBackupName = "project.unreadable.lina";
constexpr std::string_view temporarySuffix = ".tmp";
// A file being imported into media/ before its final name is known.
constexpr std::string_view importSuffix = ".import.tmp";
constexpr std::string_view stateSuffix = ".bin";
constexpr std::string_view mediaFolderName = "media";
constexpr std::size_t digitsInMediaName = 8;

std::filesystem::path temporaryFor(const std::filesystem::path &target) {
  std::filesystem::path temporary = target;
  temporary += temporarySuffix;
  return temporary;
}

// Removes a temporary file after a failure that is reported anyway; whether
// the removal works changes nothing about that report.
void dropTemporary(FileSystem &fileSystem, const std::filesystem::path &file) {
  const auto ignored = fileSystem.removeFile(file);
  static_cast<void>(ignored);
}

// Makes sure nothing is at the place of a temporary file: one left by an
// interrupted earlier attempt is removed, and so is an empty folder of that
// name. Anything else in the way is reported as what it is.
std::expected<void, Error> clearTemporary(FileSystem &fileSystem,
                                          const std::filesystem::path &file) {
  const auto removed = fileSystem.removeFile(file);
  if (removed.has_value()) {
    return {};
  }
  const auto there = fileSystem.info(file);
  if (!there.has_value() || there->kind != FileKind::Directory) {
    return removed;
  }
  if (fileSystem.removeDirectory(file).has_value()) {
    return {};
  }
  return std::unexpected(Error{
      .code = ErrorCode::NotAFile,
      .where = file.string(),
      .message = "a folder that is not empty is in the place of a temporary "
                 "file of LINA's; move it away",
  });
}

// Writes a temporary file beside `target`, complete and flushed.
std::expected<void, Error>
writeTemporary(FileSystem &fileSystem, const std::filesystem::path &target,
               std::string_view bytes,
               std::optional<std::uint32_t> permissions) {
  const std::filesystem::path temporary = temporaryFor(target);
  auto step = clearTemporary(fileSystem, temporary);
  if (step.has_value()) {
    step = detail::writeNewFile(fileSystem, temporary, bytes, permissions);
    if (!step.has_value()) {
      dropTemporary(fileSystem, temporary);
    }
  }
  return step;
}

// Gives a temporary file its final name, in one step, and flushes the folder.
std::expected<void, Error> putInPlace(FileSystem &fileSystem,
                                      const std::filesystem::path &target) {
  const std::filesystem::path temporary = temporaryFor(target);
  if (auto renamed = fileSystem.rename(temporary, target);
      !renamed.has_value()) {
    dropTemporary(fileSystem, temporary);
    return renamed;
  }
  return fileSystem.syncDirectory(target.parent_path());
}

// Puts a file in place in one step: written to a temporary file beside it,
// flushed, renamed over the target, and the folder flushed.
std::expected<void, Error>
replaceFile(FileSystem &fileSystem, const std::filesystem::path &target,
            std::string_view bytes,
            std::optional<std::uint32_t> permissions = std::nullopt) {
  auto step = writeTemporary(fileSystem, target, bytes, permissions);
  if (step.has_value()) {
    step = putInPlace(fileSystem, target);
  }
  return step;
}

// As `list`, with a folder that is not there counting as empty.
std::expected<std::vector<std::string>, Error>
listIfPresent(FileSystem &fileSystem, const std::filesystem::path &folder) {
  auto names = fileSystem.list(folder);
  if (!names.has_value() && names.error().code == ErrorCode::NotFound) {
    return std::vector<std::string>{};
  }
  return names;
}

// The content of a file, or nothing when it is not there.
std::expected<std::optional<std::string>, Error>
readIfPresent(FileSystem &fileSystem, const std::filesystem::path &file,
              std::uint64_t maxBytes) {
  auto bytes = detail::readWholeFile(fileSystem, file, maxBytes);
  if (bytes.has_value()) {
    return std::move(*bytes);
  }
  if (bytes.error().code == ErrorCode::NotFound) {
    return std::nullopt;
  }
  return std::unexpected(std::move(bytes.error()));
}

// The name inside backup/ for a document whose format version was peeked.
std::string backupNameFor(std::optional<int> version, int current) {
  if (!version.has_value()) {
    return std::string{unreadableBackupName};
  }
  if (*version < current) {
    return "project.v" + std::to_string(*version) +
           std::string{documentExtension};
  }
  return std::string{documentName};
}

// What the document refers to inside the folder and is not there.
struct Missing {
  std::vector<std::string> states;
  std::vector<std::string> media;
};

// Step 2 of saving: looks for every state file and every media file inside
// the folder that the document refers to. One that is not there (or is a
// folder) is listed, not an error: a project with missing files stays usable
// and can be saved (doc 10). Only failing to look is an error.
std::expected<Missing, Error> findMissing(FileSystem &fileSystem,
                                          const ProjectFolder &folder,
                                          const DocumentImage &image) {
  const auto isThere =
      [&fileSystem](
          const std::filesystem::path &file) -> std::expected<bool, Error> {
    const auto there = fileSystem.info(file);
    if (there.has_value()) {
      return there->kind != FileKind::Directory;
    }
    if (there.error().code == ErrorCode::NotFound) {
      return false;
    }
    return std::unexpected(there.error());
  };
  Missing missing;
  for (const std::string &name : image.stateRefs()) {
    const auto found = isThere(folder.stateFolder() / name);
    if (!found.has_value()) {
      return std::unexpected(found.error());
    }
    if (!*found) {
      missing.states.push_back(name);
    }
  }
  for (const std::string &path : image.mediaPaths()) {
    const auto found = isThere(folder.root() / path);
    if (!found.has_value()) {
      return std::unexpected(found.error());
    }
    if (!*found) {
      missing.media.push_back(path);
    }
  }
  return missing;
}

// What step 3 of saving found and prepared.
struct PreparedBackup {
  // Empty when there is no current document.
  std::string name;
  std::optional<std::uint32_t> permissions;
};

// Step 3 of saving: the current document, copied to a temporary file in
// backup/. Nothing in backup/ that has a final name is touched yet.
std::expected<PreparedBackup, Error>
prepareBackup(FileSystem &fileSystem, const ProjectFolder &folder,
              const detail::ReadingRules &rules) {
  const std::filesystem::path document = folder.documentPath();
  const auto there = fileSystem.info(document);
  if (!there.has_value()) {
    if (there.error().code == ErrorCode::NotFound) {
      return PreparedBackup{};
    }
    return std::unexpected(there.error());
  }
  if (there->kind != FileKind::Regular) {
    return std::unexpected(Error{
        .code = ErrorCode::NotAFile,
        .where = document.string(),
        .message = "this is not a regular file, and LINA does not replace "
                   "it; it was not saved",
    });
  }
  if (auto made = fileSystem.createDirectory(folder.backupFolder());
      !made.has_value()) {
    return std::unexpected(made.error());
  }
  PreparedBackup prepared{.name = {}, .permissions = there->permissions};
  // A document within the size limit is read once: for its version and as
  // the bytes to keep. One beyond it was not written by LINA; it is kept
  // all the same, copied in pieces under the name for the unreadable.
  auto current =
      detail::readWholeFile(fileSystem, document, rules.maxDocumentBytes);
  if (current.has_value()) {
    prepared.name = backupNameFor(detail::peekFormatVersion(*current),
                                  rules.history.current());
    if (auto written =
            writeTemporary(fileSystem, folder.backupFolder() / prepared.name,
                           *current, prepared.permissions);
        !written.has_value()) {
      return std::unexpected(written.error());
    }
    return prepared;
  }
  if (current.error().code != ErrorCode::TooLarge) {
    return std::unexpected(std::move(current.error()));
  }
  prepared.name = std::string{unreadableBackupName};
  const std::filesystem::path temporary =
      temporaryFor(folder.backupFolder() / prepared.name);
  auto step = clearTemporary(fileSystem, temporary);
  if (step.has_value()) {
    step =
        detail::copyFile(fileSystem, document, temporary, prepared.permissions);
    if (!step.has_value()) {
      dropTemporary(fileSystem, temporary);
    }
  }
  if (!step.has_value()) {
    return std::unexpected(std::move(step.error()));
  }
  return prepared;
}

// Step 7 of saving: recovery/ goes, with whatever the recovery copy left in
// it.
std::expected<void, Error> removeRecovery(FileSystem &fileSystem,
                                          const ProjectFolder &folder) {
  auto step = fileSystem.removeFile(folder.recoveryDocumentPath());
  if (step.has_value()) {
    step = fileSystem.removeFile(temporaryFor(folder.recoveryDocumentPath()));
  }
  if (step.has_value()) {
    step = fileSystem.removeDirectory(folder.recoveryFolder());
  }
  return step;
}

// The state references of one document on disk, added to `live`. A file that
// is not there adds nothing; one that cannot be read is an error, since then
// nothing is known about what it needs.
std::expected<void, Error>
addStatesOf(FileSystem &fileSystem, const std::filesystem::path &file,
            const detail::ReadingRules &rules,
            std::set<std::string, std::less<>> &live) {
  const auto unreadable = [&file](const Error &cause) {
    return std::unexpected(Error{
        .code = cause.code,
        .where = file.string(),
        .message = "no state file was deleted, because this document cannot "
                   "be read and may need any of them: " +
                   describe(cause),
    });
  };
  const auto bytes = readIfPresent(fileSystem, file, rules.maxDocumentBytes);
  if (!bytes.has_value()) {
    return unreadable(bytes.error());
  }
  if (!bytes->has_value()) {
    return {};
  }
  const auto loaded =
      detail::readDocumentWith(**bytes, rules.history, rules.maxDocumentBytes);
  if (!loaded.has_value()) {
    return unreadable(loaded.error());
  }
  const auto refs = detail::stateRefsOf(loaded->document.project());
  live.insert(refs.begin(), refs.end());
  return {};
}

// True when a name inside media/ carries these eight digits of a content
// hash.
bool carriesDigits(const std::string &name, std::string_view digits) {
  const std::string stem = std::filesystem::path{name}.stem().string();
  return stem.size() > digits.size() && stem.ends_with(digits) &&
         stem.at(stem.size() - digits.size() - 1) == '-';
}

// The temporary files in one folder that are LINA's own, by `isOurs`, removed.
template <class IsOurs>
std::expected<void, Error>
removeTemporariesIn(FileSystem &fileSystem, const ProjectFolder &folder,
                    std::string_view subfolder, IsOurs isOurs,
                    std::vector<std::string> &gone) {
  const std::filesystem::path path = folder.root() / subfolder;
  const auto names = listIfPresent(fileSystem, path);
  if (!names.has_value()) {
    return std::unexpected(names.error());
  }
  for (const std::string &name : *names) {
    if (!isOurs(std::string_view{name})) {
      continue;
    }
    if (auto removed = fileSystem.removeFile(path / name);
        !removed.has_value()) {
      return removed;
    }
    gone.push_back((std::filesystem::path{subfolder} / name).string());
  }
  return {};
}

// One pass over a file being imported: copied into `incoming` and hashed on
// the way, so that what is stored is what was hashed. Fills in the size and
// the hash.
std::expected<ImportedMedia, Error>
copyAndHash(FileSystem &fileSystem, InputFile &input,
            const std::filesystem::path &incoming) {
  auto output = fileSystem.createFile(incoming, std::nullopt);
  if (!output.has_value()) {
    return std::unexpected(std::move(output.error()));
  }
  ImportedMedia imported;
  Sha256 hash;
  std::vector<char> buffer(detail::copyChunkBytes);
  while (true) {
    const auto count = input.read(buffer);
    if (!count.has_value()) {
      return std::unexpected(count.error());
    }
    if (*count == 0) {
      break;
    }
    const std::string_view piece{buffer.data(), *count};
    hash.update(piece);
    imported.sizeBytes += piece.size();
    if (auto written = (*output)->write(piece); !written.has_value()) {
      return std::unexpected(std::move(written.error()));
    }
  }
  if (auto flushed = (*output)->sync(); !flushed.has_value()) {
    return std::unexpected(std::move(flushed.error()));
  }
  imported.contentHash = hash.finishHex();
  return imported;
}

// Gives an imported file its place in media/ and returns its path relative to
// the project folder: that of a file already stored with the same content,
// under whatever name, or else `name`, which `incoming` is renamed to. On
// failure `incoming` is still there.
std::expected<std::string, Error>
settleImport(FileSystem &fileSystem, const ProjectFolder &folder,
             const std::filesystem::path &incoming, const std::string &name,
             std::string_view contentHash) {
  const std::filesystem::path media = folder.mediaFolder();
  const std::string_view digits = contentHash.substr(0, digitsInMediaName);
  const auto inside = [](const std::string &stored) {
    return std::string{mediaFolderName} + "/" + stored;
  };
  const auto present = fileSystem.list(media);
  if (!present.has_value()) {
    return std::unexpected(present.error());
  }
  for (const std::string &stored : *present) {
    if (stored.ends_with(importSuffix) || !carriesDigits(stored, digits)) {
      continue;
    }
    const auto existing = detail::hashFile(fileSystem, media / stored);
    if (!existing.has_value()) {
      return std::unexpected(existing.error());
    }
    if (*existing == contentHash) {
      if (auto dropped = fileSystem.removeFile(incoming);
          !dropped.has_value()) {
        return std::unexpected(std::move(dropped.error()));
      }
      return inside(stored);
    }
  }
  if (std::ranges::binary_search(*present, name)) {
    return std::unexpected(Error{
        .code = ErrorCode::AlreadyExists,
        .where = (media / name).string(),
        .message = "a different file is stored under this name",
    });
  }
  if (auto renamed = fileSystem.rename(incoming, media / name);
      !renamed.has_value()) {
    return std::unexpected(std::move(renamed.error()));
  }
  if (auto flushed = fileSystem.syncDirectory(media); !flushed.has_value()) {
    return std::unexpected(std::move(flushed.error()));
  }
  return inside(name);
}

// "<pid>\n<host>\n<application>\n".
std::string noteText(const LockNote &note) {
  return std::to_string(note.processId) + "\n" + note.hostName + "\n" +
         note.application + "\n";
}

std::optional<LockNote> parseNote(std::string_view text) {
  std::array<std::string_view, 3> lines;
  for (std::string_view &line : lines) {
    const auto end = text.find('\n');
    if (end == std::string_view::npos) {
      return std::nullopt;
    }
    line = text.substr(0, end);
    text.remove_prefix(end + 1);
  }
  LockNote note;
  const std::string_view number = lines.front();
  const char *const last = std::to_address(number.end());
  const auto parsed =
      std::from_chars(std::to_address(number.begin()), last, note.processId);
  if (!text.empty() || parsed.ec != std::errc{} || parsed.ptr != last) {
    return std::nullopt;
  }
  note.hostName = lines.at(1);
  note.application = lines.at(2);
  return note;
}

} // namespace

namespace detail {

std::expected<void, Error> copyFile(FileSystem &fileSystem,
                                    const std::filesystem::path &from,
                                    const std::filesystem::path &to,
                                    std::optional<std::uint32_t> permissions) {
  auto input = fileSystem.openForReading(from, Links::Refuse);
  if (!input.has_value()) {
    return std::unexpected(std::move(input.error()));
  }
  auto output = fileSystem.createFile(to, permissions);
  if (!output.has_value()) {
    return std::unexpected(std::move(output.error()));
  }
  std::vector<char> buffer(copyChunkBytes);
  while (true) {
    const auto count = (*input)->read(buffer);
    if (!count.has_value()) {
      return std::unexpected(count.error());
    }
    if (*count == 0) {
      return (*output)->sync();
    }
    if (auto written =
            (*output)->write(std::string_view{buffer.data(), *count});
        !written.has_value()) {
      return written;
    }
  }
}

std::expected<SaveReport, Error> saveProject(FileSystem &fileSystem,
                                             const ProjectFolder &folder,
                                             const DocumentImage &image,
                                             const ReadingRules &rules) {
  const std::filesystem::path document = folder.documentPath();
  // Step 2.
  auto missing = findMissing(fileSystem, folder, image);
  if (!missing.has_value()) {
    return std::unexpected(std::move(missing.error()));
  }
  // Step 3. It also finds out the permissions the new file must have and
  // whether project.lina may be replaced at all.
  auto backup = prepareBackup(fileSystem, folder, rules);
  if (!backup.has_value()) {
    return std::unexpected(std::move(backup.error()));
  }
  const std::filesystem::path backupTarget =
      folder.backupFolder() / backup->name;
  const auto dropBackup = [&] {
    if (!backup->name.empty()) {
      dropTemporary(fileSystem, temporaryFor(backupTarget));
    }
  };
  // Step 4: the new document, complete and flushed, beside the old one.
  if (auto written = writeTemporary(fileSystem, document, image.bytes(),
                                    backup->permissions);
      !written.has_value()) {
    dropBackup();
    return std::unexpected(std::move(written.error()));
  }
  // Step 5: the swap.
  if (auto swapped = fileSystem.rename(temporaryFor(document), document);
      !swapped.has_value()) {
    dropTemporary(fileSystem, temporaryFor(document));
    dropBackup();
    return std::unexpected(std::move(swapped.error()));
  }
  if (auto flushed = fileSystem.syncDirectory(folder.root());
      !flushed.has_value()) {
    // project.lina holds the new document, but it is not known to be safe on
    // the disk. The previous backup still has its name; the prepared one is
    // left as a temporary file for the next save to replace.
    return std::unexpected(std::move(flushed.error()));
  }

  // From here on the save stands.
  SaveReport report;
  report.missingStates = std::move(missing->states);
  report.missingMedia = std::move(missing->media);
  // Step 6: only now does the previous backup give way.
  if (!backup->name.empty()) {
    if (auto named =
            fileSystem.rename(temporaryFor(backupTarget), backupTarget);
        named.has_value()) {
      report.backupName = backup->name;
      if (auto flushed = fileSystem.syncDirectory(folder.backupFolder());
          !flushed.has_value()) {
        report.afterSwapProblems.push_back(std::move(flushed.error()));
      }
    } else {
      dropBackup();
      report.afterSwapProblems.push_back(std::move(named.error()));
    }
  }
  // Step 7.
  if (auto discarded = removeRecovery(fileSystem, folder);
      !discarded.has_value()) {
    report.afterSwapProblems.push_back(std::move(discarded.error()));
  }
  return report;
}

std::expected<std::vector<std::string>, Error>
collectStates(FileSystem &fileSystem, const ProjectFolder &folder,
              std::span<const std::string> keep, const ReadingRules &rules) {
  std::set<std::string, std::less<>> live{keep.begin(), keep.end()};
  std::vector<std::filesystem::path> documents{
      folder.documentPath(),
      folder.recoveryDocumentPath(),
  };
  const auto backups = listIfPresent(fileSystem, folder.backupFolder());
  if (!backups.has_value()) {
    return std::unexpected(backups.error());
  }
  for (const std::string &name : *backups) {
    if (name.ends_with(documentExtension)) {
      documents.push_back(folder.backupFolder() / name);
    }
  }
  for (const std::filesystem::path &file : documents) {
    if (auto added = addStatesOf(fileSystem, file, rules, live);
        !added.has_value()) {
      return std::unexpected(std::move(added.error()));
    }
  }

  const auto present = listIfPresent(fileSystem, folder.stateFolder());
  if (!present.has_value()) {
    return std::unexpected(present.error());
  }
  std::vector<std::string> removed;
  for (const std::string &name : *present) {
    if (!isStateFileName(name) || live.contains(name)) {
      continue;
    }
    if (auto gone = fileSystem.removeFile(folder.stateFolder() / name);
        !gone.has_value()) {
      return std::unexpected(std::move(gone.error()));
    }
    removed.push_back(name);
  }
  return removed;
}

} // namespace detail

LockNote currentProcessNote(std::string application) {
  // Longer than any host name Linux allows (64 bytes), so the result always
  // ends in a null character.
  std::array<char, 256> name{};
  const bool known = ::gethostname(name.data(), name.size() - 1) == 0;
  return {
      .processId = ::getpid(),
      .hostName = known ? std::string{name.data()} : std::string{},
      .application = std::move(application),
  };
}

std::string mediaFileName(const std::filesystem::path &original,
                          std::string_view contentHash) {
  const std::filesystem::path name = original.filename();
  return name.stem().string() + "-" +
         std::string{contentHash.substr(0, digitsInMediaName)} +
         name.extension().string();
}

bool isStateFileName(std::string_view name) {
  return name.ends_with(stateSuffix) &&
         detail::isContentHash(
             name.substr(0, name.size() - stateSuffix.size()));
}

ProjectFolder::ProjectFolder(FileSystem &fileSystem, std::filesystem::path root)
    : fileSystem_(&fileSystem), root_(std::move(root)) {}

std::filesystem::path ProjectFolder::documentPath() const {
  return root_ / documentName;
}
std::filesystem::path ProjectFolder::viewPath() const {
  return root_ / viewName;
}
std::filesystem::path ProjectFolder::mediaFolder() const {
  return root_ / mediaFolderName;
}
std::filesystem::path ProjectFolder::stateFolder() const {
  return root_ / "state";
}
std::filesystem::path ProjectFolder::backupFolder() const {
  return root_ / "backup";
}
std::filesystem::path ProjectFolder::recoveryFolder() const {
  return root_ / "recovery";
}
std::filesystem::path ProjectFolder::recoveryDocumentPath() const {
  return recoveryFolder() / documentName;
}
std::filesystem::path ProjectFolder::cacheFolder() const {
  return root_ / "cache";
}
std::filesystem::path ProjectFolder::lockPath() const {
  return root_ / ".lock";
}

std::expected<void, Error> ProjectFolder::create() {
  return detail::guarded([this] -> std::expected<void, Error> {
    for (const std::filesystem::path &folder :
         {root_, mediaFolder(), stateFolder(), cacheFolder()}) {
      if (auto made = fileSystem_->createDirectory(folder); !made.has_value()) {
        return made;
      }
    }
    if (auto flushed = fileSystem_->syncDirectory(root_);
        !flushed.has_value()) {
      return flushed;
    }
    const std::filesystem::path parent = root_.parent_path();
    return fileSystem_->syncDirectory(parent.empty() ? "." : parent);
  });
}

std::expected<std::vector<std::string>, Error>
ProjectFolder::removeStaleTemporaries() {
  return detail::guarded([this]
                         -> std::expected<std::vector<std::string>, Error> {
    std::vector<std::string> gone;
    const std::string documentTemporary =
        std::string{documentName} + std::string{temporarySuffix};
    const std::string viewTemporary =
        std::string{viewName} + std::string{temporarySuffix};
    const std::string backupEnding =
        std::string{documentExtension} + std::string{temporarySuffix};
    auto swept = removeTemporariesIn(
        *fileSystem_, *this, "",
        [&](std::string_view name) {
          return name == documentTemporary || name == viewTemporary;
        },
        gone);
    if (swept.has_value()) {
      swept = removeTemporariesIn(
          *fileSystem_, *this, "backup",
          [&](std::string_view name) {
            return name.starts_with("project.") && name.ends_with(backupEnding);
          },
          gone);
    }
    if (swept.has_value()) {
      swept = removeTemporariesIn(
          *fileSystem_, *this, "recovery",
          [&](std::string_view name) { return name == documentTemporary; },
          gone);
    }
    if (swept.has_value()) {
      swept = removeTemporariesIn(
          *fileSystem_, *this, "state",
          [](std::string_view name) {
            return name.ends_with(temporarySuffix) &&
                   isStateFileName(
                       name.substr(0, name.size() - temporarySuffix.size()));
          },
          gone);
    }
    if (swept.has_value()) {
      swept = removeTemporariesIn(
          *fileSystem_, *this, mediaFolderName,
          [](std::string_view name) { return name.ends_with(importSuffix); },
          gone);
    }
    if (!swept.has_value()) {
      return std::unexpected(std::move(swept.error()));
    }
    return gone;
  });
}

std::expected<LoadedDocument, Error> ProjectFolder::load() {
  return detail::guarded([this] -> std::expected<LoadedDocument, Error> {
    const auto bytes =
        detail::readWholeFile(*fileSystem_, documentPath(), maxDocumentBytes);
    if (!bytes.has_value()) {
      return std::unexpected(bytes.error());
    }
    return readDocument(*bytes);
  });
}

std::expected<SaveReport, Error>
ProjectFolder::save(const DocumentImage &image) {
  return detail::guarded([&] {
    return detail::saveProject(*fileSystem_, *this, image,
                               {
                                   .history = detail::formatHistory(),
                                   .maxDocumentBytes = maxDocumentBytes,
                               });
  });
}

std::expected<std::vector<std::string>, Error>
ProjectFolder::collectStates(std::span<const std::string> keep) {
  return detail::guarded([&] {
    return detail::collectStates(*fileSystem_, *this, keep,
                                 {
                                     .history = detail::formatHistory(),
                                     .maxDocumentBytes = maxDocumentBytes,
                                 });
  });
}

std::expected<void, Error>
ProjectFolder::writeRecovery(const DocumentImage &image) {
  return detail::guarded([&] -> std::expected<void, Error> {
    if (auto made = fileSystem_->createDirectory(recoveryFolder());
        !made.has_value()) {
      return made;
    }
    return replaceFile(*fileSystem_, recoveryDocumentPath(), image.bytes());
  });
}

std::expected<std::optional<RecoveryInfo>, Error>
ProjectFolder::findRecovery() {
  return detail::guarded(
      [this] -> std::expected<std::optional<RecoveryInfo>, Error> {
        const auto recovery = readIfPresent(
            *fileSystem_, recoveryDocumentPath(), maxDocumentBytes);
        if (!recovery.has_value()) {
          return std::unexpected(recovery.error());
        }
        if (!recovery->has_value()) {
          return std::nullopt;
        }
        const auto saved =
            readIfPresent(*fileSystem_, documentPath(), maxDocumentBytes);
        if (!saved.has_value()) {
          return std::unexpected(saved.error());
        }
        if (*saved == *recovery) {
          return std::nullopt;
        }
        const auto written = fileSystem_->info(recoveryDocumentPath());
        if (!written.has_value()) {
          return std::unexpected(written.error());
        }
        return RecoveryInfo{.written = written->modified};
      });
}

std::expected<LoadedDocument, Error> ProjectFolder::loadRecovery() {
  return detail::guarded([this] -> std::expected<LoadedDocument, Error> {
    const auto bytes = detail::readWholeFile(
        *fileSystem_, recoveryDocumentPath(), maxDocumentBytes);
    if (!bytes.has_value()) {
      return std::unexpected(bytes.error());
    }
    return readDocument(*bytes);
  });
}

std::expected<void, Error> ProjectFolder::discardRecovery() {
  return detail::guarded(
      [this] { return removeRecovery(*fileSystem_, *this); });
}

std::expected<std::string, Error>
ProjectFolder::storeState(std::string_view bytes) {
  return detail::guarded([&] -> std::expected<std::string, Error> {
    const std::string hash = sha256Hex(bytes);
    std::string name = hash + std::string{stateSuffix};
    const std::filesystem::path target = stateFolder() / name;
    // A file of that name is trusted only if it holds the content; one cut
    // short or damaged is replaced.
    const auto existing = detail::hashFile(*fileSystem_, target);
    if (existing.has_value() && *existing == hash) {
      return name;
    }
    if (!existing.has_value() && existing.error().code != ErrorCode::NotFound) {
      return std::unexpected(existing.error());
    }
    if (auto made = fileSystem_->createDirectory(stateFolder());
        !made.has_value()) {
      return std::unexpected(made.error());
    }
    if (auto stored = replaceFile(*fileSystem_, target, bytes);
        !stored.has_value()) {
      return std::unexpected(stored.error());
    }
    return name;
  });
}

std::expected<std::string, Error>
ProjectFolder::loadState(std::string_view name) {
  return detail::guarded([&] -> std::expected<std::string, Error> {
    if (!isStateFileName(name)) {
      return std::unexpected(Error{
          .code = ErrorCode::Value,
          .where = std::string{name},
          .message = "not the name of a state file",
      });
    }
    const std::filesystem::path file = stateFolder() / name;
    auto bytes = detail::readWholeFile(*fileSystem_, file, maxStateBytes);
    if (bytes.has_value() &&
        sha256Hex(*bytes) + std::string{stateSuffix} != name) {
      return std::unexpected(Error{
          .code = ErrorCode::Corrupt,
          .where = file.string(),
          .message = "the content does not match the name",
      });
    }
    return bytes;
  });
}

std::expected<ImportedMedia, Error>
ProjectFolder::importMedia(const std::filesystem::path &source) {
  return detail::guarded([&] -> std::expected<ImportedMedia, Error> {
    const std::string fileName = source.filename().string();
    if (fileName.empty() || !detail::isValidUtf8(fileName) ||
        fileName.contains('\0')) {
      return std::unexpected(Error{
          .code = ErrorCode::Value,
          .where = source.string(),
          .message = "the file name is empty or not UTF-8 text",
      });
    }
    auto input = fileSystem_->openForReading(source, Links::Follow);
    if (!input.has_value()) {
      return std::unexpected(std::move(input.error()));
    }
    if (auto made = fileSystem_->createDirectory(mediaFolder());
        !made.has_value()) {
      return std::unexpected(made.error());
    }
    const std::filesystem::path incoming =
        mediaFolder() / (fileName + std::string{importSuffix});
    if (auto cleared = clearTemporary(*fileSystem_, incoming);
        !cleared.has_value()) {
      return std::unexpected(cleared.error());
    }
    auto imported = copyAndHash(*fileSystem_, **input, incoming);
    if (imported.has_value()) {
      auto path = settleImport(*fileSystem_, *this, incoming,
                               mediaFileName(source, imported->contentHash),
                               imported->contentHash);
      if (path.has_value()) {
        imported->path = std::move(*path);
        return imported;
      }
      imported = std::unexpected(std::move(path.error()));
    }
    dropTemporary(*fileSystem_, incoming);
    return imported;
  });
}

std::optional<std::filesystem::path>
ProjectFolder::mediaLocation(std::string_view documentPath) const {
  if (detail::isProjectMediaPath(documentPath)) {
    return root_ / documentPath;
  }
  if (detail::isExternalMediaPath(documentPath)) {
    return std::filesystem::path{documentPath};
  }
  return std::nullopt;
}

std::expected<void, Error> ProjectFolder::saveView(const ViewState &state) {
  return detail::guarded([&] -> std::expected<void, Error> {
    const auto text = writeViewState(state);
    if (!text.has_value()) {
      return std::unexpected(text.error());
    }
    return replaceFile(*fileSystem_, viewPath(), *text);
  });
}

std::expected<ViewState, Error> ProjectFolder::loadView() {
  return detail::guarded([this] -> std::expected<ViewState, Error> {
    const auto bytes =
        detail::readWholeFile(*fileSystem_, viewPath(), maxDocumentBytes);
    if (!bytes.has_value()) {
      return std::unexpected(bytes.error());
    }
    return readViewState(*bytes);
  });
}

std::expected<ProjectLock, Error>
ProjectFolder::acquireLock(const LockNote &note) {
  return detail::guarded([&] -> std::expected<ProjectLock, Error> {
    auto attempt = fileSystem_->lock(lockPath(), noteText(note));
    if (!attempt.has_value()) {
      return std::unexpected(std::move(attempt.error()));
    }
    ProjectLock result{
        .state = attempt->state,
        .held = std::move(attempt->lock),
        .holder = std::nullopt,
    };
    if (result.state == LockState::HeldElsewhere) {
      result.holder = parseNote(attempt->note);
    }
    return result;
  });
}

} // namespace lina::format
