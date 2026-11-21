// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format and its tests: the chain of migrations
// (docs/design/16-project-file-format-encoding.md, "Versions and migration").
// A migration rewrites the parsed tree of version N into that of version N+1;
// it never sees a model class.
#ifndef LINA_FORMAT_INTERNAL_MIGRATION_HPP
#define LINA_FORMAT_INTERNAL_MIGRATION_HPP

#include "internal/json_tree.hpp"
#include "lina/format/document_io.hpp"
#include "lina/format/error.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lina::format::detail {

// What a migration is told and what it can say back. Passed by reference so
// that members can be added without touching the migrations already written.
struct MigrationContext {
  // The version of the tree the step is given.
  int fromVersion{0};
  // The version the file had when it was read, before any step.
  int fileVersion{0};
  // The file's "written_by", for steps that must tell apart what different
  // releases wrote under one version; empty when the file has none.
  std::string writtenBy;
  // For a step that cannot carry something over exactly: one sentence each,
  // in words for the user. They end up in `LoadedDocument`.
  std::vector<std::string> warnings;
};

// Turns the tree of one format version into the tree of the next, or says why
// it cannot. The reader sets the "format" member afterwards.
using Migration = std::optional<Error> (*)(JsonValue &root,
                                           MigrationContext &context);

// Every format version LINA ever wrote: `migrations[i]` leads from version
// `oldest + i` to the next, and the last one arrives at the current version.
struct FormatHistory {
  int oldest{formatVersion};
  std::span<const Migration> migrations;

  [[nodiscard]] constexpr int current() const {
    return oldest + static_cast<int>(migrations.size());
  }
};

// The history of the real format.
FormatHistory formatHistory();

// `readDocument` over a given history and size limit: check the size, parse,
// check the version, refuse a newer one, migrate step by step, map strictly
// to the model, validate.
std::expected<LoadedDocument, Error>
readDocumentWith(std::string_view bytes, const FormatHistory &history,
                 std::size_t maxBytes = maxDocumentBytes);

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_MIGRATION_HPP
