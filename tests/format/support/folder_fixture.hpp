// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: a project folder in the scratch folder, on a file system that
// logs and can be made to fail.
#ifndef LINA_FORMAT_TESTS_SUPPORT_FOLDER_FIXTURE_HPP
#define LINA_FORMAT_TESTS_SUPPORT_FOLDER_FIXTURE_HPP

#include "support/documents.hpp"
#include "support/faulty_file_system.hpp"
#include "support/scratch.hpp"

#include "lina/format/project_folder.hpp"

#include <filesystem>
#include <string_view>

namespace lina::format::testing {

// `name` names the test case's scratch folder. The project folder is created
// and the log is empty afterwards.
struct FolderFixture {
  explicit FolderFixture(std::string_view name)
      : root(freshScratch(name) / "My Song"), files(root), folder(files, root),
        created(folder.create().has_value()) {
    files.heal();
  }

  std::filesystem::path root;
  FaultyFileSystem files;
  ProjectFolder folder;
  bool created;
};

// The saved form of a document that can be written.
inline DocumentImage imageOf(const model::Document &document) {
  return writeDocument(document, testWriter).value();
}

// Puts the files a document refers to inside the folder where a save expects
// them: its state files (with content that does not hash to their names,
// which a save does not look at) and its media files.
inline void provideReferenced(const ProjectFolder &folder,
                              const DocumentImage &image) {
  std::filesystem::create_directories(folder.stateFolder());
  for (const std::string &name : image.stateRefs()) {
    if (!std::filesystem::exists(folder.stateFolder() / name)) {
      writeText(folder.stateFolder() / name, "state");
    }
  }
  for (const std::string &path : image.mediaPaths()) {
    std::filesystem::create_directories((folder.root() / path).parent_path());
    if (!std::filesystem::exists(folder.root() / path)) {
      writeText(folder.root() / path, "audio");
    }
  }
}

} // namespace lina::format::testing

#endif // LINA_FORMAT_TESTS_SUPPORT_FOLDER_FIXTURE_HPP
