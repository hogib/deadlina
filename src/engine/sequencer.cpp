// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/sequencer.hpp"

#include <algorithm>
#include <utility>

namespace lina::engine::internal {

namespace te = tracktion;

SequencerPlugin::SequencerPlugin(const te::PluginCreationInfo &info)
    : Plugin(info) {}

SequencerPlugin::~SequencerPlugin() { notifyListenersOfDeletion(); }

juce::ValueTree SequencerPlugin::create() {
  return te::createValueTree(te::IDs::PLUGIN, te::IDs::type, xmlTypeName);
}

void SequencerPlugin::publish(std::vector<SequencedClip> clips,
                              double sampleRate) {
  publish(std::move(clips), sampleRate, false);
}

void SequencerPlugin::publish(std::vector<SequencedClip> clips,
                              double sampleRate, bool newOutlet) {
  auto publication = std::make_unique<Publication>();
  publication->sequence = std::make_shared<const TrackSequence>(
      ++lastNumber_, sampleRate, std::move(clips));
  const std::size_t room = roomFor(*publication->sequence);
  if (newOutlet || published_.empty() ||
      published_.back()->outlet->capacity() < room) {
    // The audio thread starts on the new outlet with this publication; the
    // old one goes when the publications that hold it go. Buffers are made
    // only while a playback graph holds the plugin; initialise() makes them
    // otherwise.
    publication->outlet = std::make_shared<MidiOutlet>();
    if (inGraph_) {
      publication->outlet->prepare(room);
    } else {
      publication->outlet->releaseFor(room);
    }
  } else {
    publication->outlet = published_.back()->outlet;
    if (inGraph_) {
      publication->outlet->supply();
    }
  }
  const Publication *raw = publication.get();
  published_.push_back(std::move(publication));
  current_.store(raw);
  retire();
}

std::shared_ptr<const TrackSequence> SequencerPlugin::published() const {
  return published_.empty() ? nullptr : published_.back()->sequence;
}

void SequencerPlugin::requestStart() { startPending_.store(true); }

std::uint64_t SequencerPlugin::blocksDropped() const {
  return blocksDropped_.load(std::memory_order_relaxed);
}

MidiOutlet *SequencerPlugin::outlet() const {
  return published_.empty() ? nullptr : published_.back()->outlet.get();
}

std::size_t SequencerPlugin::room() const {
  return outlet() == nullptr ? 0 : outlet()->capacity();
}

std::size_t SequencerPlugin::buffersReady() const {
  return outlet() == nullptr ? 0 : outlet()->freshCount();
}

bool SequencerPlugin::buffersAccountedFor() const {
  return outlet() == nullptr ||
         outlet()->accountedCount() == outlet()->ownedCount();
}

std::size_t SequencerPlugin::roomFor(const TrackSequence &sequence) const {
  constexpr std::size_t fixed = (2 * midiPitchCount) + incomingRoom;
  std::size_t events = 0;
  for (const SequencedClip &clip : sequence.clips()) {
    events += clip.playback->events().events.size();
  }
  // Working out the densest stretch reads every event; it is not needed
  // when the room there is takes all of them.
  if (fixed + events > room()) {
    events = sequence.densestStretch(blockFrames_);
  }
  const std::size_t needed =
      (fixed + events + roomStep - 1) / roomStep * roomStep;
  return std::clamp(needed, leastRoom, mostRoom);
}

void SequencerPlugin::retire() {
  // Stored before this is loaded, in one total order with the audio thread's
  // store of the mark and its second load (take()).
  const Publication *used = inUse_.load();
  const Publication *newest =
      published_.empty() ? nullptr : published_.back().get();
  std::erase_if(published_,
                [newest, used](const std::unique_ptr<const Publication> &p) {
                  return p.get() != newest && p.get() != used;
                });
}

void SequencerPlugin::initialise(const te::PluginInitialisationInfo &info) {
  ++graphsPrepared_;
  sampleRate_.store(info.sampleRate);
  blockFrames_ = std::max(assumedBlockFrames,
                          static_cast<std::int64_t>(info.blockSizeSamples));
  if (inGraph_) {
    // The device has changed while an older graph still holds the plugin,
    // and the audio thread may be running it: what it uses is left alone,
    // and the newest sequence goes out again with buffers for the new
    // graph.
    if (!published_.empty()) {
      const auto newest = published_.back()->sequence;
      publish(newest->clips(), newest->sampleRate(), true);
    }
    return;
  }
  // No graph holds the plugin: the audio thread does not run it.
  inGraph_ = true;
  player_.reset();
  retire();
  if (auto *current = outlet()) {
    current->prepare(
        std::max(current->capacity(), roomFor(*published_.back()->sequence)));
  }
}

void SequencerPlugin::initialiseWithoutStopping(
    [[maybe_unused]] const te::PluginInitialisationInfo &info) {
  ++graphsPrepared_;
  retire();
  if (auto *current = outlet()) {
    current->supply();
  }
}

void SequencerPlugin::deinitialise() {
  // The last graph that held the plugin is gone, and with it the engine's
  // buffers that were the sequencer's.
  inGraph_ = false;
  player_.reset();
  retire();
  if (auto *current = outlet()) {
    current->release();
  }
}

const SequencerPlugin::Publication *SequencerPlugin::take()
    [[clang::nonblocking]] {
  // Marked, then checked to be still current: once both agree, the main
  // thread sees the mark before it frees anything (retire()).
  const Publication *latest = current_.load();
  for (;;) {
    inUse_.store(latest);
    const Publication *again = current_.load();
    if (again == latest) {
      return latest;
    }
    latest = again;
  }
}

void SequencerPlugin::applyToBuffer(const te::PluginRenderContext &context)
    [[clang::nonblocking]] {
  auto *out = context.bufferForMidiMessages;
  if (out == nullptr) {
    return;
  }
  if (out->isAllNotesOff) {
    player_.forgetAll();
  }
  if (const Publication *latest = take()) {
    play(*latest, context, *out);
  }
  // Nothing was ever published, or the block is done with what was.
  inUse_.store(nullptr, std::memory_order_release);
}

void SequencerPlugin::play(const Publication &latest,
                           const te::PluginRenderContext &context,
                           te::MidiMessageArray &out) [[clang::nonblocking]] {
  const double sampleRate = sampleRate_.load(std::memory_order_relaxed);
  const TrackSequence *sequence = latest.sequence->sampleRate() == sampleRate
                                      ? latest.sequence.get()
                                      : nullptr;
  const bool playing = context.isPlaying;
  const SequencerBlock block{
      .start = te::toSamples(context.editTime.getStart(), sampleRate),
      .length = context.bufferNumSamples,
      .playing = playing,
      .starting = playing && startPending_.exchange(false),
  };

  const std::size_t ours = player_.bound(sequence, block);
  if (ours == 0) {
    // Nothing to send; the record moves on with the block.
    player_.play(sequence, block, out, sampleRate);
    return;
  }
  auto *buffer =
      latest.outlet->acquire(static_cast<std::size_t>(out.size()) + ours);
  if (buffer == nullptr) {
    if (player_.soundingCount() > 0) {
      out.isAllNotesOff = true;
    }
    player_.skip(sequence, block);
    blocksDropped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  for (auto &message : out) {
    buffer->add(std::move(message));
  }
  buffer->isAllNotesOff = out.isAllNotesOff;
  player_.play(sequence, block, *buffer, sampleRate);
  // What came in and what the sequencer adds, in order of time, a note-off
  // before a note-on at the same time. The sort does not allocate.
  buffer->sortByTimestamp();
  latest.outlet->handOver(out);
}

} // namespace lina::engine::internal
