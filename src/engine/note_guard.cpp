// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/note_guard.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace lina::engine::internal {

namespace te = tracktion;

namespace {

/// The channel the projection plays every clip's notes on.
constexpr int midiChannel = 1;

} // namespace

NoteSchedule::NoteSchedule(std::span<const ClipEvents *const> clips) {
  // In the order the clips play, each pitch's notes come out in order of
  // their starts without sorting notes: clips do not overlap, and within a
  // clip a pitch's note-ons and note-offs alternate.
  std::vector<const ClipEvents *> ordered(clips.begin(), clips.end());
  std::ranges::sort(ordered, {}, &ClipEvents::startFrame);
  std::array<std::size_t, pitchCount> counts{};
  for (const ClipEvents *clip : ordered) {
    for (const NoteEvent &event : clip->events) {
      counts.at(event.pitch % pitchCount) += event.isNoteOn() ? 1 : 0;
    }
  }
  std::array<std::size_t, pitchCount> next{};
  for (std::size_t pitch = 0; pitch < pitchCount; ++pitch) {
    next.at(pitch) = first_.at(pitch);
    first_.at(pitch + 1) = first_.at(pitch) + counts.at(pitch);
  }
  intervals_.resize(first_.at(pitchCount));
  for (const ClipEvents *clip : ordered) {
    for (const NoteEvent &event : clip->events) {
      const std::size_t pitch = event.pitch % pitchCount;
      if (event.isNoteOn()) {
        intervals_.at(next.at(pitch)).on = event.frame;
      } else {
        intervals_.at(next.at(pitch)++).off = event.frame;
      }
    }
  }
}

bool NoteSchedule::holds(std::size_t pitch, std::int64_t frame) const
    [[clang::nonblocking]] {
  // The last note of the pitch that began before the frame, if any. The
  // indices are within the vector by construction.
  std::size_t low = first_.at(pitch % pitchCount);
  std::size_t high = first_.at((pitch % pitchCount) + 1);
  const std::size_t none = low;
  while (low < high) {
    const std::size_t middle = low + ((high - low) / 2);
    if (intervals_.at(middle).on < frame) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low != none && intervals_.at(low - 1).off >= frame;
}

NoteGuardPlugin::NoteGuardPlugin(const te::PluginCreationInfo &info)
    : Plugin(info) {}

NoteGuardPlugin::~NoteGuardPlugin() {
  notifyListenersOfDeletion();
  forget();
}

juce::ValueTree NoteGuardPlugin::create() {
  return te::createValueTree(te::IDs::PLUGIN, te::IDs::type, xmlTypeName);
}

void NoteGuardPlugin::watch(std::shared_ptr<const NoteSchedule> schedule) {
  releaseRetired();
  const NoteSchedule *raw = schedule.get();
  published_.push_back(
      {.number = ++lastNumber_, .schedule = std::move(schedule)});
  // The number goes out after the pointer: the audio thread reads the
  // number first, so the schedule it then reads is that one or a later one,
  // and both stay alive until it reports a later number.
  schedule_.store(raw);
  scheduleNumber_.store(lastNumber_);
  // A buffer with room goes along, and the one the audio thread has used
  // comes back. Both exchanges hand over ownership; the unique_ptr frees
  // what comes back to this thread.
  auto roomy = std::make_unique<te::MidiMessageArray>();
  roomy->reserve(bufferRoom);
  const std::unique_ptr<te::MidiMessageArray> used(used_.exchange(nullptr));
  const std::unique_ptr<te::MidiMessageArray> unused(
      roomy_.exchange(roomy.release()));
  state_.store(watching);
}

void NoteGuardPlugin::graphHandedOver() {
  int expected = watching;
  state_.compare_exchange_strong(expected, settleBlocks);
}

void NoteGuardPlugin::forget() {
  // Only called while the playback graph is down: nothing reads the
  // schedules.
  state_.store(idle);
  schedule_.store(nullptr);
  published_.clear();
  const std::unique_ptr<te::MidiMessageArray> used(used_.exchange(nullptr));
  const std::unique_ptr<te::MidiMessageArray> unused(roomy_.exchange(nullptr));
}

bool NoteGuardPlugin::isWatching() const { return state_.load() != idle; }

void NoteGuardPlugin::releaseRetired() {
  const std::uint64_t taken = takenNumber_.load();
  std::erase_if(published_, [taken](const Published &entry) {
    return entry.number < taken;
  });
}

void NoteGuardPlugin::initialise(const te::PluginInitialisationInfo &info) {
  sampleRate_ = info.sampleRate;
  sounding_.fill(false);
}

void NoteGuardPlugin::deinitialise() { sounding_.fill(false); }

void NoteGuardPlugin::reset() { sounding_.fill(false); }

void NoteGuardPlugin::endNotes(te::MidiMessageArray &messages,
                               std::int64_t frame, const NoteSchedule &schedule)
    [[clang::nonblocking]] {
  int ending = 0;
  for (std::size_t pitch = 0; pitch < pitchCount; ++pitch) {
    // The index is below the size of the array, so at() never throws.
    ending += sounding_.at(pitch) && !schedule.holds(pitch, frame) ? 1 : 0;
  }
  if (ending == 0) {
    return;
  }
  // The note-offs go first, at the start of the block, and what the block
  // held follows in its order: sorted without sorting.
  te::MidiMessageArray *roomy = messages.size() + ending <= bufferRoom
                                    ? roomy_.exchange(nullptr)
                                    : nullptr;
  if (roomy == nullptr) {
    messages.isAllNotesOff = true;
    sounding_.fill(false);
    return;
  }
  for (std::size_t pitch = 0; pitch < pitchCount; ++pitch) {
    if (sounding_.at(pitch) && !schedule.holds(pitch, frame)) {
      roomy->addMidiMessage(
          juce::MidiMessage::noteOff(midiChannel, static_cast<int>(pitch)), 0.0,
          te::MPESourceID{});
    }
  }
  for (auto &message : messages) {
    roomy->add(std::move(message));
  }
  roomy->isAllNotesOff = messages.isAllNotesOff;
  messages.swapWith(*roomy);
  // The slot is empty: the message thread empties it before it hands over
  // the next buffer with room, and this one was taken only once.
  used_.store(roomy);
}

void NoteGuardPlugin::applyToBuffer(const te::PluginRenderContext &context)
    [[clang::nonblocking]] {
  auto *messages = context.bufferForMidiMessages;
  if (messages == nullptr) {
    return;
  }
  if (messages->isAllNotesOff) {
    sounding_.fill(false);
  }

  const int state = state_.load();
  if (state != idle && context.isPlaying) {
    // The number first, then the schedule: see watch().
    const std::uint64_t number = scheduleNumber_.load();
    const NoteSchedule *schedule = schedule_.load();
    takenNumber_.store(number);
    endNotes(*messages, te::toSamples(context.editTime.getStart(), sampleRate_),
             *schedule);
    if (state > idle) {
      // Counts down to idle. A watch that starts in between wins.
      int expected = state;
      state_.compare_exchange_strong(expected, state - 1);
    }
  }

  for (const auto &message : *messages) {
    if (message.isNoteOn()) {
      sounding_.at(static_cast<std::size_t>(message.getNoteNumber()) %
                   pitchCount) = true;
    } else if (message.isNoteOff()) {
      sounding_.at(static_cast<std::size_t>(message.getNoteNumber()) %
                   pitchCount) = false;
    }
  }
}

} // namespace lina::engine::internal
