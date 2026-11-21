// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/reader_invariant.hpp"

#include "lina/format/document_io.hpp"
#include "lina/format/view_state.hpp"

namespace lina::format::testing {

ReadOutcome readOutcome(std::string_view bytes) {
  const auto loaded = readDocument(bytes);
  if (!loaded.has_value()) {
    return loaded.error().message.empty() ? ReadOutcome::Broken
                                          : ReadOutcome::Refused;
  }
  const model::Project &project = loaded->document.project();
  if (!model::validate(project).empty()) {
    return ReadOutcome::Broken;
  }
  const auto image = writeDocument(loaded->document, loaded->writtenBy);
  if (!image.has_value()) {
    return ReadOutcome::Broken;
  }
  const auto again = readDocument(image->bytes());
  if (!again.has_value() || again->document.project() != project ||
      again->writtenBy != loaded->writtenBy) {
    return ReadOutcome::Broken;
  }
  const auto imageAgain = writeDocument(again->document, again->writtenBy);
  const bool stable =
      imageAgain.has_value() && imageAgain->bytes() == image->bytes();
  return stable ? ReadOutcome::Accepted : ReadOutcome::Broken;
}

ReadOutcome viewReadOutcome(std::string_view bytes) {
  const auto state = readViewState(bytes);
  if (!state.has_value()) {
    return state.error().message.empty() ? ReadOutcome::Broken
                                         : ReadOutcome::Refused;
  }
  const auto text = writeViewState(*state);
  if (!text.has_value()) {
    return ReadOutcome::Broken;
  }
  const auto again = readViewState(*text);
  return again.has_value() && *again == *state ? ReadOutcome::Accepted
                                               : ReadOutcome::Broken;
}

} // namespace lina::format::testing
