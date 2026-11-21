// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project document as bytes
// (docs/design/16-project-file-format-encoding.md): LINA's own canonical JSON
// writer and a strict reader. The same document, written by the same
// application, always gives the same bytes; reading gives a whole valid
// document or an error, never part of one. Neither function throws.
#ifndef LINA_FORMAT_DOCUMENT_IO_HPP
#define LINA_FORMAT_DOCUMENT_IO_HPP

#include "lina/format/error.hpp"
#include "lina/model/document.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace lina::format {

// The format version this build writes, and the newest it reads.
inline constexpr int formatVersion = 1;

// The largest document, in bytes, that is read or written: 256 MiB. Reading
// builds a tree some twenty times the size of the text, so the limit is what
// keeps a hostile file from exhausting memory.
inline constexpr std::size_t maxDocumentBytes = std::size_t{256} * 1024 * 1024;

// A document in its saved form, ready to be handed to a worker that writes it
// (project_folder.hpp). Made by `writeDocument` only, so what reaches the disk
// is always something the reader takes back.
class DocumentImage {
public:
  // The canonical text of the document.
  [[nodiscard]] const std::string &bytes() const { return bytes_; }
  // The names of the plugin state files the document refers to, sorted, each
  // once.
  [[nodiscard]] const std::vector<std::string> &stateRefs() const {
    return stateRefs_;
  }
  // The paths of the media files the document says are inside the project
  // folder (relative paths), sorted, each once.
  [[nodiscard]] const std::vector<std::string> &mediaPaths() const {
    return mediaPaths_;
  }

private:
  DocumentImage(std::string bytes, std::vector<std::string> stateRefs,
                std::vector<std::string> mediaPaths)
      : bytes_(std::move(bytes)), stateRefs_(std::move(stateRefs)),
        mediaPaths_(std::move(mediaPaths)) {}

  friend std::expected<DocumentImage, Error>
  writeDocument(const model::Document &document, std::string_view writtenBy);

  std::string bytes_;
  std::vector<std::string> stateRefs_;
  std::vector<std::string> mediaPaths_;
};

// Writes a document in the current format version. `writtenBy` names the
// writing application and its version ("LINA 0.1.0"); it is stored as
// "written_by" and has no effect on loading. Fails, naming the object, for
// anything the reader would not take back: a document that does not pass the
// model's `validate`, a media path of neither allowed shape, a `writtenBy`
// that is not UTF-8 text, a text larger than `maxDocumentBytes`.
std::expected<DocumentImage, Error>
writeDocument(const model::Document &document, std::string_view writtenBy);

struct LoadedDocument {
  model::Document document;
  // The format version of the bytes that were read; older than `formatVersion`
  // when the document was migrated on the way in.
  int formatRead{formatVersion};
  // The "written_by" of the bytes that were read.
  std::string writtenBy;
  // What migrations could not carry over exactly, in words for the user.
  // Empty for a document of the current version.
  std::vector<std::string> migrationWarnings;
};

// Reads a document of this or any earlier format version.
std::expected<LoadedDocument, Error> readDocument(std::string_view bytes);

} // namespace lina::format

#endif // LINA_FORMAT_DOCUMENT_IO_HPP
