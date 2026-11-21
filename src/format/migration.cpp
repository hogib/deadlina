// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal/migration.hpp"

#include "internal/codec.hpp"
#include "internal/guard.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <variant>

namespace lina::format::detail {

namespace {

Error versionError(std::string message) {
  return {
      .code = ErrorCode::Version,
      .where = "/format",
      .message = std::move(message),
  };
}

// Finds the "format" member and checks it against the history. Returns the
// version, or the reason the document cannot be read.
std::expected<int, Error> findVersion(const JsonValue &root,
                                      const FormatHistory &history) {
  const auto *object = std::get_if<JsonValue::Object>(&root.data);
  if (object == nullptr) {
    return std::unexpected(Error{
        .code = ErrorCode::Structure,
        .where = {},
        .message =
            "expected an object, found " + std::string{jsonTypeName(root)},
    });
  }
  const JsonValue *format = findMember(*object, "format");
  if (format == nullptr) {
    return std::unexpected(Error{
        .code = ErrorCode::Structure,
        .where = {},
        .message = "missing key \"format\"",
    });
  }
  const auto *version = std::get_if<std::uint64_t>(&format->data);
  if (version == nullptr) {
    return std::unexpected(Error{
        .code = ErrorCode::Structure,
        .where = "/format",
        .message = "expected a format version number, found " +
                   std::string{jsonTypeName(*format)},
    });
  }
  const int current = history.current();
  if (std::cmp_greater(*version, current)) {
    return std::unexpected(versionError(
        "this project was written by a newer LINA (format version " +
        std::to_string(*version) + "); this one reads up to version " +
        std::to_string(current)));
  }
  if (std::cmp_less(*version, history.oldest)) {
    return std::unexpected(versionError("format version " +
                                        std::to_string(*version) +
                                        " is not one LINA ever wrote"));
  }
  return static_cast<int>(*version);
}

// The "written_by" of a tree of any version: empty unless it is a string.
std::string writtenByOf(const JsonValue &root) {
  const auto &object = std::get<JsonValue::Object>(root.data);
  const JsonValue *member = findMember(object, "written_by");
  const auto *text =
      member == nullptr ? nullptr : std::get_if<std::string>(&member->data);
  return text == nullptr ? std::string{} : *text;
}

std::expected<LoadedDocument, Error> read(std::string_view bytes,
                                          const FormatHistory &history,
                                          std::size_t maxBytes) {
  if (bytes.size() > maxBytes) {
    return std::unexpected(tooLargeError(bytes.size(), maxBytes));
  }
  auto tree = parseJson(bytes);
  if (!tree.has_value()) {
    return std::unexpected(std::move(tree.error()));
  }
  const auto fileVersion = findVersion(*tree, history);
  if (!fileVersion.has_value()) {
    return std::unexpected(fileVersion.error());
  }
  MigrationContext context{
      .fromVersion = *fileVersion,
      .fileVersion = *fileVersion,
      .writtenBy = writtenByOf(*tree),
      .warnings = {},
  };
  for (const Migration migrate : history.migrations.subspan(
           static_cast<std::size_t>(*fileVersion - history.oldest))) {
    if (auto refused = migrate(*tree, context)) {
      return std::unexpected(std::move(*refused));
    }
    ++context.fromVersion;
    // The step may have rebuilt the tree, so the member is looked up again;
    // a step that dropped it is caught by the strict mapping.
    auto *object = std::get_if<JsonValue::Object>(&tree->data);
    JsonValue *member =
        object == nullptr ? nullptr : findMember(*object, "format");
    if (member != nullptr) {
      member->data = static_cast<std::uint64_t>(context.fromVersion);
    }
  }

  auto decoded = decodeProject(*tree);
  if (!decoded.has_value()) {
    return std::unexpected(std::move(decoded.error()));
  }
  auto document = model::Document::fromProject(std::move(decoded->project));
  if (!document.has_value()) {
    return std::unexpected(ruleError(document.error()));
  }
  return LoadedDocument{
      .document = std::move(*document),
      .formatRead = *fileVersion,
      .writtenBy = std::move(decoded->writtenBy),
      .migrationWarnings = std::move(context.warnings),
  };
}

} // namespace

FormatHistory formatHistory() { return {}; }

std::expected<LoadedDocument, Error>
readDocumentWith(std::string_view bytes, const FormatHistory &history,
                 std::size_t maxBytes) {
  return guarded([&] { return read(bytes, history, maxBytes); });
}

} // namespace lina::format::detail

namespace lina::format {

std::expected<LoadedDocument, Error> readDocument(std::string_view bytes) {
  return detail::readDocumentWith(bytes, detail::formatHistory());
}

} // namespace lina::format
