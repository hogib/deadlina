// SPDX-License-Identifier: GPL-3.0-or-later
//
// For tests of the projection: a readable account of the edit an Engine
// holds, and audio files to play and to compare with.
#ifndef LINA_ENGINE_TESTING_PROJECTION_SUPPORT_HPP
#define LINA_ENGINE_TESTING_PROJECTION_SUPPORT_HPP

#include "lina/engine/audio_buffer.hpp"
#include "lina/engine/engine.hpp"

#include <filesystem>
#include <string>

namespace lina::engine::testing {

/// The state of the engine's edit as text, one line per thing, taken from
/// the engine's own objects: tempo, tracks in order with their plugins and
/// clips, levels, sends, and for every MIDI track the clips and events its
/// sequencer was given last. Objects are named by the LINA identity the
/// projection maps to them, never by the engine's own identifiers, so that
/// two edits that play the same document read the same however they were
/// built. An engine object the projection does not account for, and an
/// entry of the projection without its engine object or missing from its
/// track's sequence, are listed as such.
std::string describeEdit(Engine &engine);

/// Writes a 32-bit floating-point WAV file.
void writeWav(const std::filesystem::path &file, const AudioBuffer &audio);

/// Reads a WAV file of any sample format as floating point.
AudioBuffer readWav(const std::filesystem::path &file);

} // namespace lina::engine::testing

#endif // LINA_ENGINE_TESTING_PROJECTION_SUPPORT_HPP
