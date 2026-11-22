// SPDX-License-Identifier: GPL-3.0-or-later
//
// LINA's own sequencer: a plugin first in the chain of every MIDI track that
// plays the track's MIDI clips to the track's instrument
// (docs/design/13-architecture.md, open question 2). The engine is given no
// MIDI clips at all, so an edit of notes or MIDI clips changes nothing in the
// engine's edit and its playback graph is not rebuilt.
//
// Main thread to audio thread. What the track plays is a TrackSequence
// (sequence.hpp), made on the main thread and never changed afterwards. It is
// published by storing a pointer to a publication, which holds the sequence
// and the outlet its messages go out through, in one atomic; the sequence
// carries its own number, which grows with every publication. At the start of
// every block the audio thread loads that pointer, marks the publication as
// in use in a second atomic and loads the pointer again; it uses the
// publication only once the two loads agree, and clears the mark when the
// block is done. The main thread frees every publication but the newest and
// the one marked in use, at every publication and whenever a playback graph
// is prepared or taken down. Both sides store before they load, in one total
// order, so a publication the audio thread is about to use is either still
// current or marked before the main thread looks. At most two publications
// are ever kept, however long the audio thread does not run. Nothing is
// allocated, locked or freed on the audio thread.
//
// What the audio thread does with it is NotePlayer's (note_player.hpp): it
// keeps its own record of the notes it started and ends exactly those. The
// frame a block starts on is the engine's playhead, read from the block's
// edit time. Playback beginning is said by the main thread (requestStart(),
// before the graph is built for play), and the first playing block after
// that chases the notes sounding across its first frame. A sequence made for
// another sample rate than the one the plugin was last prepared with is not
// played.
//
// A new graph that holds the plugin while an older graph still does is
// prepared without touching what the audio thread uses: the engine asks for
// that with initialiseWithoutStopping(), and with initialise() when the
// device's rate or block size has changed. In the second case the newest
// sequence is published again with buffers made for the new graph, and the
// rate the audio thread compares against is the one place that changes.
//
// Its messages reach the instrument through a MidiOutlet (midi_outlet.hpp),
// which hands the engine buffers with room reserved on the main thread. The
// room is worked out from the sequence: the most events in any stretch of one
// block, plus a note-off and a note-on for every pitch, plus room for
// messages that come in from upstream. A sequence that needs more room than
// the outlet has is published with a new outlet. A block that needs more all
// the same is not sent at all: the instrument is told to end every note, the
// sequencer forgets what it started, and the block is counted
// (blocksDropped()). A note may then be missing; none is left hanging.
#ifndef LINA_ENGINE_INTERNAL_SEQUENCER_HPP
#define LINA_ENGINE_INTERNAL_SEQUENCER_HPP

#include "lina/engine/internal/midi_outlet.hpp"
#include "lina/engine/internal/note_player.hpp"
#include "lina/engine/internal/sequence.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace lina::engine::internal {

class SequencerPlugin final : public tracktion::Plugin {
public:
  using Ptr = juce::ReferenceCountedObjectPtr<SequencerPlugin>;

  static constexpr const char *xmlTypeName = "linaSequencer";
  /// Room kept in every block for messages that come in from upstream.
  static constexpr std::size_t incomingRoom = 128;
  /// The least and the most room a block's buffer is made with, and the
  /// step in which the room grows.
  static constexpr std::size_t leastRoom = 384;
  static constexpr std::size_t mostRoom = 65'536;
  static constexpr std::size_t roomStep = 64;
  /// The block size the room is worked out for until the plugin has been
  /// prepared for a device (docs/design/11-non-functional-requirements.md,
  /// "Audio", rule 7: the largest buffer).
  static constexpr std::int64_t assumedBlockFrames = 4'096;

  explicit SequencerPlugin(const tracktion::PluginCreationInfo &info);
  ~SequencerPlugin() override;

  SequencerPlugin(const SequencerPlugin &) = delete;
  SequencerPlugin &operator=(const SequencerPlugin &) = delete;
  SequencerPlugin(SequencerPlugin &&) = delete;
  SequencerPlugin &operator=(SequencerPlugin &&) = delete;

  static const char *getPluginName() { return "LINA sequencer"; }
  static juce::ValueTree create();

  // --- Main thread ---

  /// From the next block on, the track plays `clips`, worked out for
  /// `sampleRate`.
  void publish(std::vector<SequencedClip> clips, double sampleRate);
  /// What was published last; null before the first publication.
  [[nodiscard]] std::shared_ptr<const TrackSequence> published() const;
  /// Playback begins: the first playing block chases. Call while no graph
  /// plays, before the one for playback is built.
  void requestStart();
  /// How many playback graphs have been prepared with this plugin in them.
  /// Every graph the engine builds holds every track's plugins, so this
  /// counts the graphs built while the track exists.
  [[nodiscard]] std::uint64_t graphsPrepared() const { return graphsPrepared_; }
  /// How many blocks were not sent for want of room.
  [[nodiscard]] std::uint64_t blocksDropped() const;
  /// How many publications are kept alive, the newest included.
  [[nodiscard]] std::size_t sequencesKept() const { return published_.size(); }
  /// The room the buffers of the newest publication are made with, and how
  /// many fresh ones are ready for the audio thread.
  [[nodiscard]] std::size_t room() const;
  [[nodiscard]] std::size_t buffersReady() const;
  /// True when every buffer of the newest publication's outlet is held by
  /// the audio thread, ready, or sent back: none is lost. Only while the
  /// audio thread does not run the plugin.
  [[nodiscard]] bool buffersAccountedFor() const;

  juce::String getName() const override { return getPluginName(); }
  juce::String getPluginType() override { return xmlTypeName; }
  juce::String getSelectableDescription() override { return getName(); }
  bool takesMidiInput() override { return true; }

  void initialise(const tracktion::PluginInitialisationInfo &info) override;
  void initialiseWithoutStopping(
      const tracktion::PluginInitialisationInfo &info) override;
  void deinitialise() override;
  void applyToBuffer(const tracktion::PluginRenderContext &context)
      [[clang::nonblocking]] override;

private:
  struct Publication {
    std::shared_ptr<const TrackSequence> sequence;
    std::shared_ptr<MidiOutlet> outlet;
  };

  /// Publishes with a new outlet when `newOutlet` is set or the room asks
  /// for one.
  void publish(std::vector<SequencedClip> clips, double sampleRate,
               bool newOutlet);
  /// Frees the publications the audio thread does not read.
  void retire();
  /// Loads the current publication and marks it in use; null when there is
  /// none.
  const Publication *take() [[clang::nonblocking]];
  /// The block, played from `latest`.
  void play(const Publication &latest,
            const tracktion::PluginRenderContext &context,
            tracktion::MidiMessageArray &out) [[clang::nonblocking]];
  /// The room a block's buffer needs for `sequence`.
  [[nodiscard]] std::size_t roomFor(const TrackSequence &sequence) const;
  /// The newest publication's outlet, or null.
  [[nodiscard]] MidiOutlet *outlet() const;

  // Main thread.
  std::vector<std::unique_ptr<const Publication>> published_;
  std::uint64_t lastNumber_{0};
  std::uint64_t graphsPrepared_{0};
  std::int64_t blockFrames_{assumedBlockFrames};
  /// Whether a playback graph holds the plugin.
  bool inGraph_{false};

  // Both threads.
  std::atomic<const Publication *> current_{nullptr};
  std::atomic<const Publication *> inUse_{nullptr};
  std::atomic<bool> startPending_{false};
  std::atomic<std::uint64_t> blocksDropped_{0};
  /// The rate of the graph last prepared.
  std::atomic<double> sampleRate_{0.0};

  // Audio thread, or main thread while no graph holds the plugin.
  NotePlayer player_;
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_SEQUENCER_HPP
