// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: documents to write and read back, each built through
// commands, so they are valid by construction.
#ifndef LINA_FORMAT_TESTS_SUPPORT_DOCUMENTS_HPP
#define LINA_FORMAT_TESTS_SUPPORT_DOCUMENTS_HPP

#include "lina/model/document.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace lina::format::testing {

// What the tests and the golden files name as the writing application.
inline constexpr std::string_view testWriter = "LINA tests 1";

// The document of tests/golden/v1/minimal.lina: one scene and one MIDI track
// whose slot holds a clip of two notes.
model::Document minimalDocument();

// The document of tests/golden/v1/everything.lina: every kind of track, clips
// in slots and on the arrangement, audio and MIDI content, notes, controller
// data, devices with parameters and a state reference, sends, automation for
// every kind of target, every kind of track input, media inside and outside
// the project, scenes, the loop range, and text that needs escaping.
model::Document everythingDocument();

// The name of the state file `everythingDocument` refers to, and its content.
std::string everythingStateContent();

// A document after `steps` commands of the model's seeded generator, applied
// one transaction each; the commands it refuses are skipped.
model::Document generatedDocument(std::uint64_t seed, int steps);

// Applies one command, which must succeed: the process stops otherwise, with
// the fault on the error stream.
model::ChangeSet mustApply(model::Document &document,
                           const model::Command &command);

} // namespace lina::format::testing

#endif // LINA_FORMAT_TESTS_SUPPORT_DOCUMENTS_HPP
