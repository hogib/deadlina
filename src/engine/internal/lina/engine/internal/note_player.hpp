// SPDX-License-Identifier: GPL-3.0-or-later
//
// The audio thread's part of LINA's own sequencer (sequencer.hpp): what it
// sends to the track's instrument in one block, given what the track plays
// (sequence.hpp) and where the block lies.
//
// The player keeps its own record of the notes it has started, by pitch. It
// sends a note-off only for a pitch it started, and it ends everything it
// started when it has to, so no note it started is left without its end and
// no note it did not start is ever ended.
//
// Per block [s, s + length), in this order (docs/design/08-midi-clips-and-
// note-editing.md, "Playback"):
//   - Not playing: ends every note it has started. The next playing block is
//     not taken to follow on.
//   - Playback begins with this block: ends every note it has started and
//     starts the notes that began before s and end after it, on frame s
//     (rule 3, chasing).
//   - Otherwise, when the block does not follow the one before or the track
//     plays another sequence since then (an edit, a change of tempo): ends
//     every note it has started that the sequence does not hold across s,
//     that is a note of that pitch that began before s and ends on s or
//     later. It starts nothing: a note that covers s is heard from its next
//     start (rule 9).
//   - Then sends the events of [s, s + length) on their frames. A note-on
//     marks its pitch as started, a note-off of a started pitch ends it.
//
// Every message is timed in seconds from the start of the block, a note-off
// an eighth of a frame into its frame and a note-on three eighths: an
// instrument that rounds and one that truncates both put it on its frame, and
// sorting by time keeps a note-off before a note-on of the same frame,
// including where one clip ends and the next begins.
#ifndef LINA_ENGINE_INTERNAL_NOTE_PLAYER_HPP
#define LINA_ENGINE_INTERNAL_NOTE_PLAYER_HPP

#include "lina/engine/internal/sequence.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace lina::engine::internal {

/// Notes carry no channel in the document
/// (docs/design/08-midi-clips-and-note-editing.md, "Playback", rule 8); the
/// instrument gets them all on this one.
inline constexpr int sequencerMidiChannel = 1;

/// Where a note-off and a note-on lie inside their frame, in frames.
inline constexpr double noteOffOffsetFrames = 0.125;
inline constexpr double noteOnOffsetFrames = 0.375;

/// One block as the sequencer sees it.
struct SequencerBlock {
  /// The first frame of the block, counted from the start of the
  /// arrangement.
  std::int64_t start{0};
  std::int64_t length{0};
  bool playing{false};
  /// Playback begins with this block.
  bool starting{false};
};

class NotePlayer {
public:
  /// Nothing has been started and no block is expected. For when the
  /// instrument is set up afresh.
  void reset() [[clang::nonblocking]];

  /// The instrument has been told to end every note.
  void forgetAll() [[clang::nonblocking]];

  /// The most messages play() can send for this block.
  [[nodiscard]] std::size_t bound(const TrackSequence *sequence,
                                  const SequencerBlock &block) const
      [[clang::nonblocking]];

  /// Appends the block's messages to `out`; `sequence` may be null, which
  /// plays nothing. `out` must have room for bound() more messages.
  void play(const TrackSequence *sequence, const SequencerBlock &block,
            tracktion::MidiMessageArray &out, double sampleRate)
      [[clang::nonblocking]];

  /// Lets the block pass without sending anything, as when there was no
  /// room for it: every note counts as ended, which the caller makes true by
  /// telling the instrument to end all notes.
  void skip(const TrackSequence *sequence, const SequencerBlock &block)
      [[clang::nonblocking]];

  [[nodiscard]] bool isSounding(std::size_t pitch) const {
    return sounding_.at(pitch % midiPitchCount);
  }
  [[nodiscard]] std::size_t soundingCount() const [[clang::nonblocking]];

private:
  /// No block is expected: the next one does not follow on.
  static constexpr std::int64_t nowhere = -1;

  /// True when what sounds must be checked against the sequence at the start
  /// of the block.
  [[nodiscard]] bool mustCheck(const TrackSequence *sequence,
                               const SequencerBlock &block) const
      [[clang::nonblocking]];
  void endAll(tracktion::MidiMessageArray &out, double sampleRate)
      [[clang::nonblocking]];
  void endUnheld(const TrackSequence *sequence, std::int64_t frame,
                 tracktion::MidiMessageArray &out, double sampleRate)
      [[clang::nonblocking]];
  void chase(const TrackSequence &sequence, std::int64_t frame,
             tracktion::MidiMessageArray &out, double sampleRate)
      [[clang::nonblocking]];
  void playEvents(const TrackSequence &sequence, const SequencerBlock &block,
                  tracktion::MidiMessageArray &out, double sampleRate)
      [[clang::nonblocking]];
  void moveOn(const TrackSequence *sequence, const SequencerBlock &block)
      [[clang::nonblocking]];

  std::array<bool, midiPitchCount> sounding_{};
  std::int64_t expectedFrame_{nowhere};
  /// The number of the sequence the last block played; 0 for none.
  std::uint64_t playingNumber_{0};
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_NOTE_PLAYER_HPP
