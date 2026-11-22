// SPDX-License-Identifier: GPL-3.0-or-later
//
// What LINA's own sequencer plays (sequencer.hpp), as the audio thread reads
// it: the notes of MIDI clips in sample frames, worked out on the main thread
// and never changed once made.
//
//   - A ClipPlayback is one MIDI clip under one time map: its note events
//     (midi_events.hpp) and the same notes by pitch, for asking which note
//     is sounding across a frame.
//   - A TrackSequence is everything one MIDI track plays: its clips that
//     play anything, in order. Clips of a track do not overlap, so neither
//     do two notes of one pitch, and a note never starts before the note of
//     its pitch that came before it has ended.
//
// A clip that nobody edits keeps its ClipPlayback from one sequence to the
// next; a new sequence for a track is a new list of the same objects.
#ifndef LINA_ENGINE_INTERNAL_SEQUENCE_HPP
#define LINA_ENGINE_INTERNAL_SEQUENCE_HPP

#include "lina/engine/internal/midi_events.hpp"
#include "lina/engine/internal/time_map.hpp"
#include "lina/model/ids.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace lina::engine::internal {

inline constexpr std::size_t midiPitchCount = 128;

/// One note as it sounds: [on, off) in frames.
struct PlayedNote {
  std::int64_t on{0};
  std::int64_t off{0};
  std::uint8_t pitch{0};
  std::uint8_t velocity{0};

  friend bool operator==(const PlayedNote &, const PlayedNote &) = default;
};

/// What one MIDI clip plays under one time map.
class ClipPlayback {
public:
  ClipPlayback(const MidiClipContent &content, const TimeMap &time);
  explicit ClipPlayback(ClipEvents events);

  [[nodiscard]] const ClipEvents &events() const { return events_; }
  [[nodiscard]] std::int64_t startFrame() const { return events_.startFrame; }
  [[nodiscard]] std::int64_t endFrame() const { return events_.endFrame; }

  /// The note of `pitch` that began before `frame` and ends on it or later,
  /// or null.
  [[nodiscard]] const PlayedNote *across(std::size_t pitch,
                                         std::int64_t frame) const
      [[clang::nonblocking]];

  /// How many events lie in [from, to).
  [[nodiscard]] std::size_t eventsIn(std::int64_t from, std::int64_t to) const
      [[clang::nonblocking]];

  /// The index of the first event on `frame` or later.
  [[nodiscard]] std::size_t firstEventFrom(std::int64_t frame) const
      [[clang::nonblocking]];

private:
  ClipEvents events_;
  /// The notes of pitch p in [first_[p], first_[p + 1]), by start.
  std::vector<PlayedNote> notes_;
  std::array<std::size_t, midiPitchCount + 1> first_{};
};

/// One clip of a track's sequence.
struct SequencedClip {
  model::ClipId clip;
  std::shared_ptr<const ClipPlayback> playback;
};

/// Everything one MIDI track plays, under one time map and sample rate.
class TrackSequence {
public:
  /// Keeps the clips that play anything, sorted by their start. The clips
  /// must not overlap.
  TrackSequence(std::uint64_t number, double sampleRate,
                std::vector<SequencedClip> clips);

  /// Set by the sequencer that publishes it; later ones have higher numbers.
  [[nodiscard]] std::uint64_t number() const { return number_; }
  [[nodiscard]] double sampleRate() const { return sampleRate_; }
  [[nodiscard]] const std::vector<SequencedClip> &clips() const {
    return clips_;
  }

  /// The index of the first clip that ends on `frame` or later.
  [[nodiscard]] std::size_t firstEndingFrom(std::int64_t frame) const
      [[clang::nonblocking]];

  /// The note of `pitch` that began before `frame` and ends on it or later,
  /// or null.
  [[nodiscard]] const PlayedNote *across(std::size_t pitch,
                                         std::int64_t frame) const
      [[clang::nonblocking]];

  /// How many events lie in [from, to).
  [[nodiscard]] std::size_t eventsIn(std::int64_t from, std::int64_t to) const
      [[clang::nonblocking]];

  /// The most events that lie in any stretch of `frames` frames.
  [[nodiscard]] std::size_t densestStretch(std::int64_t frames) const;

private:
  std::uint64_t number_;
  double sampleRate_;
  std::vector<SequencedClip> clips_;
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_SEQUENCE_HPP
