// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a MIDI clip plays, worked out from the document's ticks alone: every
// note-on and note-off with the sample frame it falls on. LINA's own sequencer
// plays these (sequence.hpp, sequencer.hpp); the engine is given no MIDI
// clips (docs/design/13-architecture.md).
//
// Rules (docs/design/08-midi-clips-and-note-editing.md, "Playback"):
//   - A note sounds when its start lies inside the stretch of content being
//     played. A note that began before that stretch does not sound.
//   - A note is cut at the end of the clip and, when the clip loops, at the
//     end of the loop.
//   - Content plays from the clip's content start. With the loop on it runs
//     to the loop end and then repeats the loop range; when the content start
//     lies at or behind the loop end, it runs straight on and never loops.
//   - Each edge is placed on the nearest sample frame. A note-off and a
//     note-on of the same pitch on one frame come out off first. A note
//     shorter than a frame lasts one frame, unless a note of the same pitch
//     starts on that frame too, in which case it is left out.
#ifndef LINA_ENGINE_INTERNAL_MIDI_EVENTS_HPP
#define LINA_ENGINE_INTERNAL_MIDI_EVENTS_HPP

#include "lina/engine/internal/time_map.hpp"
#include "lina/model/clip.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lina::engine::internal {

/// The most notes one clip plays. A looped clip is unrolled over its whole
/// length, and a short loop in a very long clip would otherwise produce
/// events without bound. Past this number the rest of the clip is silent.
inline constexpr std::size_t maxSoundingNotesPerClip = 250'000;

/// The parts of a MIDI clip that decide what it plays.
struct MidiClipContent {
  model::ClipExtent extent;
  model::LoopSettings loop;
  /// Sorted by start, as the document keeps them.
  std::vector<model::Note> notes;

  friend bool operator==(const MidiClipContent &,
                         const MidiClipContent &) = default;
};

/// A note-on (velocity 1 to 127) or a note-off (velocity 0) on a sample frame
/// of the arrangement.
struct NoteEvent {
  std::int64_t frame{0};
  std::uint8_t pitch{0};
  std::uint8_t velocity{0};

  [[nodiscard]] bool isNoteOn() const { return velocity != 0; }

  friend bool operator==(const NoteEvent &, const NoteEvent &) = default;
};

struct ClipEvents {
  /// In playing order: by frame, note-offs before note-ons on one frame.
  std::vector<NoteEvent> events;
  /// The first frame of the clip and the first frame after what it plays,
  /// which is its end or the engine's horizon. Every event lies in
  /// [startFrame, endFrame].
  std::int64_t startFrame{0};
  std::int64_t endFrame{0};
  /// True when maxSoundingNotesPerClip cut the clip short.
  bool truncated{false};

  friend bool operator==(const ClipEvents &, const ClipEvents &) = default;
};

[[nodiscard]] ClipEvents noteEvents(const MidiClipContent &clip,
                                    const TimeMap &time);

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_MIDI_EVENTS_HPP
