// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format and its tests: the folder operations that read
// documents already on disk, over a given format history and size limit, so
// that a test can work with a document of an older format version before the
// real format has one, and with an oversized one without writing 256 MiB.
#ifndef LINA_FORMAT_INTERNAL_SAVE_HPP
#define LINA_FORMAT_INTERNAL_SAVE_HPP

#include "internal/migration.hpp"
#include "lina/format/project_folder.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace lina::format::detail {

struct ReadingRules {
  FormatHistory history;
  std::size_t maxDocumentBytes{lina::format::maxDocumentBytes};
};

// `ProjectFolder::save`.
std::expected<SaveReport, Error> saveProject(FileSystem &fileSystem,
                                             const ProjectFolder &folder,
                                             const DocumentImage &image,
                                             const ReadingRules &rules);

// `ProjectFolder::collectStates`.
std::expected<std::vector<std::string>, Error>
collectStates(FileSystem &fileSystem, const ProjectFolder &folder,
              std::span<const std::string> keep, const ReadingRules &rules);

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_SAVE_HPP
