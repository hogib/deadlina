// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the test instrument and effect must produce, computed here from the
// definitions in test_plugins.h without using their code.
#ifndef LINA_TESTS_ENGINE_REFERENCE_SIGNAL_HPP
#define LINA_TESTS_ENGINE_REFERENCE_SIGNAL_HPP

#include "lina/engine/audio_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

namespace lina::engine::testing {

/// Largest difference allowed between engine output and a signal computed
/// from the instrument's formula. One part in a million is about eight steps
/// of single precision at full scale: room for a compiler evaluating the
/// cosine differently, and far below one bit of 16-bit audio. As measured,
/// GCC debug builds agree exactly.
inline constexpr float formulaTolerance = 1.0e-6F;

/// A note as it must sound: full level from `onFrame`, silent from
/// `offFrame`, scaled by `gain`.
struct SoundingNote {
  int pitch = 60;
  int velocity = 127;
  std::int64_t onFrame = 0;
  std::int64_t offFrame = 0;
  float gain = 1.0F;
};

/// The frame from which a note that nominally ends at `nominalEndFrame` is
/// silent when the engine reads the note from a clip built by hand.
/// Tracktion Engine v3.2.0 sends every such note-off 0.0001 s early
/// (MidiNote::getPlaybackTime, "nudge the note-up backwards"), which is 4.8
/// frames at 48 kHz; the instrument rounds event times to the nearest frame.
/// A clip of a document does not go this way: LINA's own sequencer plays
/// its notes, and they end on their exact frame.
inline std::int64_t releaseFrame(std::int64_t nominalEndFrame,
                                 double sampleRate) {
  return std::lround(static_cast<double>(nominalEndFrame) -
                     (0.0001 * sampleRate));
}

inline float toneSample(const SoundingNote &note, std::int64_t frame,
                        double sampleRate) {
  const double frequency = 440.0 * std::pow(2.0, (note.pitch - 69) / 12.0);
  const double phase = 2.0 * std::numbers::pi * frequency *
                       static_cast<double>(frame - note.onFrame) / sampleRate;
  return note.gain *
         static_cast<float>(note.velocity / 127.0 * std::cos(phase));
}

/// One channel of `frameCount` frames holding the sum of the notes.
inline std::vector<float> referenceSignal(std::int64_t frameCount,
                                          double sampleRate,
                                          std::span<const SoundingNote> notes) {
  std::vector<float> signal(static_cast<std::size_t>(frameCount), 0.0F);
  for (const auto &note : notes) {
    const auto from = std::clamp<std::int64_t>(note.onFrame, 0, frameCount);
    const auto to = std::clamp<std::int64_t>(note.offFrame, from, frameCount);
    for (auto frame = from; frame < to; ++frame) {
      signal.at(static_cast<std::size_t>(frame)) +=
          toneSample(note, frame, sampleRate);
    }
  }
  return signal;
}

/// Largest absolute difference between two channels; infinite if their
/// lengths differ.
inline float maxDifference(std::span<const float> a, std::span<const float> b) {
  if (a.size() != b.size()) {
    return std::numeric_limits<float>::infinity();
  }
  float worst = 0.0F;
  auto other = b.begin();
  for (const float sample : a) {
    worst = std::max(worst, std::abs(sample - *other));
    ++other;
  }
  return worst;
}

/// True when both hold the same channels with the same samples, bit for
/// bit. A comparison through this function reports one line when it fails,
/// where comparing the vectors directly would print every sample.
inline bool identical(const AudioBuffer &a, const AudioBuffer &b) {
  return a.sampleRate == b.sampleRate && a.channels == b.channels;
}

/// Index of the first sample that is not zero, or -1 for silence.
inline std::int64_t firstSoundingFrame(std::span<const float> channel) {
  const auto found = std::ranges::find_if(
      channel, [](float sample) { return sample != 0.0F; });
  return found == channel.end() ? -1 : found - channel.begin();
}

} // namespace lina::engine::testing

#endif // LINA_TESTS_ENGINE_REFERENCE_SIGNAL_HPP
