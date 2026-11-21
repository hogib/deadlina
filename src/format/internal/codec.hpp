// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format and its tests: the two directions between a project
// and the JSON of the current format version.
#ifndef LINA_FORMAT_INTERNAL_CODEC_HPP
#define LINA_FORMAT_INTERNAL_CODEC_HPP

#include "internal/json_tree.hpp"
#include "lina/format/error.hpp"
#include "lina/model/project.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lina::format::detail {

// "track 12", "note 40", "project 0": an object of the model for a message.
std::string describeObject(model::ObjectRef object);

// The message for a project that breaks rules of the model.
Error ruleError(const std::vector<model::Failure> &failures);

// The error for a document of `size` bytes where `maxBytes` is the limit.
Error tooLargeError(std::size_t size, std::size_t maxBytes);

// What stops a project from being written so that the reader takes it back,
// if anything: a broken rule of the model, a media path of neither allowed
// shape, a `writtenBy` that is not storable text.
std::optional<Error> findUnwritable(const model::Project &project,
                                    std::string_view writtenBy);

// The canonical text of a project that passes `findUnwritable`.
std::string encodeProject(const model::Project &project,
                          std::string_view writtenBy);

// The state references of a project, sorted, each once.
std::vector<std::string> stateRefsOf(const model::Project &project);

// The media paths of a project that lie inside the project folder, sorted,
// each once.
std::vector<std::string> projectMediaPathsOf(const model::Project &project);

// A project mapped from a tree, and the "written_by" beside it.
struct Decoded {
  model::Project project;
  std::string writtenBy;
};

// Maps the tree of a document of the current version to a project, strictly.
// The "format" member must be present; its value is the caller's business.
// The result still has to pass `validate`.
std::expected<Decoded, Error> decodeProject(const JsonValue &root);

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_CODEC_HPP
