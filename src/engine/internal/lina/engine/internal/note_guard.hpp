// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keeps notes from hanging when a MIDI clip is edited while it plays.
//
// The engine's node for a MIDI clip does not remember which notes it has
// started. When an edit takes a sounding note's end away (the note is
// removed, shortened to before the playhead or moved, its clip goes, the
// tempo changes), the rebuilt playback graph never sends that note-off.
//
// A NoteGuardPlugin sits first in the chain of every MIDI track and watches
// the note-ons and note-offs that really pass, on the audio thread. After an
// edit the projection hands it a NoteSchedule: every note the track's clips
// now play, as frames. From then until the rebuilt graph has taken over, the
// guard looks at the start of every block at each pitch it has heard start:
// if the schedule has no note of that pitch that began before this block and
// ends in it or later, nobody is going to end it, and the guard ends it at
// once. That decision is made where the truth is: with the notes that were
// delivered and the frame the audio thread is on, not with a guess from the
// message thread. It therefore also catches a note that the old graph
// starts between the edit and the moment the new graph takes over.
//
// Ending a note means putting a note-off into the block's MIDI buffer,
// which belongs to the engine and may have no room left; making room would
// allocate on the audio thread. With every schedule the guard is therefore
// given a buffer with room of its own, which it fills and swaps for the
// engine's. That serves one block with notes to end per edit. Should a
// second such block come before the next edit, or should the block hold
// more messages than the buffer has room for, the guard asks the instrument
// to end all notes instead, through the flag the engine has for that.
//
// What it does not tell apart yet: notes from clips and notes played live.
// A live note on a pitch the schedule does not hold would be ended during
// the few blocks around an edit. There is no live input before recording
// and monitoring are built.
#ifndef LINA_ENGINE_INTERNAL_NOTE_GUARD_HPP
#define LINA_ENGINE_INTERNAL_NOTE_GUARD_HPP

#include "lina/engine/internal/midi_events.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace lina::engine::internal {

/// Every note a track's clips play: [on, off) in frames, by pitch.
class NoteSchedule {
public:
  /// From the events of each clip of one track. The clips of a track do not
  /// overlap, so no two notes of one pitch do.
  explicit NoteSchedule(std::span<const ClipEvents *const> clips);

  /// True when a note of `pitch` began before `frame` and its note-off lies
  /// on `frame` or later: it is sounding when the block that starts on
  /// `frame` begins, and whoever plays this schedule will end it.
  [[nodiscard]] bool holds(std::size_t pitch, std::int64_t frame) const
      [[clang::nonblocking]];

private:
  struct Interval {
    std::int64_t on{0};
    std::int64_t off{0};
  };

  static constexpr std::size_t pitchCount = 128;

  /// All intervals, those of pitch p in [first_[p], first_[p + 1]), each
  /// stretch sorted by `on`.
  std::vector<Interval> intervals_;
  std::array<std::size_t, pitchCount + 1> first_{};
};

class NoteGuardPlugin final : public tracktion::Plugin {
public:
  using Ptr = juce::ReferenceCountedObjectPtr<NoteGuardPlugin>;

  static constexpr const char *xmlTypeName = "linaNoteGuard";
  /// How many blocks the guard keeps looking after it was told that the
  /// rebuilt graph is in place. The audio thread takes a new graph at the
  /// start of a block, so one block after the hand-over may still be the
  /// old graph's; the rest is margin.
  static constexpr int settleBlocks = 3;
  /// How many messages the buffer handed over with a schedule has room for.
  static constexpr int bufferRoom = 1'024;

  explicit NoteGuardPlugin(const tracktion::PluginCreationInfo &info);
  ~NoteGuardPlugin() override;

  NoteGuardPlugin(const NoteGuardPlugin &) = delete;
  NoteGuardPlugin &operator=(const NoteGuardPlugin &) = delete;
  NoteGuardPlugin(NoteGuardPlugin &&) = delete;
  NoteGuardPlugin &operator=(NoteGuardPlugin &&) = delete;

  static const char *getPluginName() { return "LINA note guard"; }
  static juce::ValueTree create();

  // --- Message thread ---

  /// Starts a watch against what the track plays from now on. Call before
  /// the playback graph is rebuilt.
  void watch(std::shared_ptr<const NoteSchedule> schedule);
  /// The rebuilt graph has been handed to the audio thread: the watch ends
  /// `settleBlocks` blocks from now. Does nothing without a watch.
  void graphHandedOver();
  /// Playback starts afresh; nothing is sounding and nothing is watched.
  void forget();
  /// True while a watch is on, as far as the message thread can tell.
  [[nodiscard]] bool isWatching() const;

  juce::String getName() const override { return getPluginName(); }
  juce::String getPluginType() override { return xmlTypeName; }
  juce::String getSelectableDescription() override { return getName(); }
  bool takesMidiInput() override { return true; }

  void initialise(const tracktion::PluginInitialisationInfo &info) override;
  void deinitialise() override;
  void reset() override;
  void applyToBuffer(const tracktion::PluginRenderContext &context)
      [[clang::nonblocking]] override;

private:
  static constexpr std::size_t pitchCount = 128;
  /// Watch states. A number above `watching` counts the blocks left.
  static constexpr int idle = 0;
  static constexpr int watching = -1;

  void releaseRetired();
  void endNotes(tracktion::MidiMessageArray &messages, std::int64_t frame,
                const NoteSchedule &schedule) [[clang::nonblocking]];

  /// Message thread: every schedule the audio thread may still be reading,
  /// with the number it was published under.
  struct Published {
    std::uint64_t number{0};
    std::shared_ptr<const NoteSchedule> schedule;
  };
  std::vector<Published> published_;
  std::uint64_t lastNumber_{0};

  /// Handed to the audio thread: the schedule to check against, its number,
  /// and the state of the watch.
  std::atomic<const NoteSchedule *> schedule_{nullptr};
  std::atomic<std::uint64_t> scheduleNumber_{0};
  std::atomic<int> state_{idle};
  /// Written by the audio thread: the number of the schedule it last took
  /// up. Older ones are free to go.
  std::atomic<std::uint64_t> takenNumber_{0};
  /// A buffer with room for the audio thread to take, and the one it has
  /// used and hands back to be freed. Both are owned by whoever holds the
  /// pointer: the atomics pass ownership between the two threads.
  std::atomic<tracktion::MidiMessageArray *> roomy_{nullptr};
  std::atomic<tracktion::MidiMessageArray *> used_{nullptr};

  /// Audio thread only.
  std::array<bool, pitchCount> sounding_{};
  double sampleRate_ = 0.0;
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_NOTE_GUARD_HPP
