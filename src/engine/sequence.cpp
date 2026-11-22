// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/sequence.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace lina::engine::internal {

ClipPlayback::ClipPlayback(const MidiClipContent &content, const TimeMap &time)
    : ClipPlayback(noteEvents(content, time)) {}

ClipPlayback::ClipPlayback(ClipEvents events) : events_(std::move(events)) {
  // Per pitch the events alternate on, off, on, off (noteEvents keeps notes
  // of one pitch apart), so the notes come out by start without sorting.
  std::array<std::size_t, midiPitchCount> counts{};
  for (const NoteEvent &event : events_.events) {
    counts.at(event.pitch % midiPitchCount) += event.isNoteOn() ? 1 : 0;
  }
  std::array<std::size_t, midiPitchCount> next{};
  for (std::size_t pitch = 0; pitch < midiPitchCount; ++pitch) {
    next.at(pitch) = first_.at(pitch);
    first_.at(pitch + 1) = first_.at(pitch) + counts.at(pitch);
  }
  notes_.resize(first_.at(midiPitchCount));
  for (const NoteEvent &event : events_.events) {
    const std::size_t pitch = event.pitch % midiPitchCount;
    if (event.isNoteOn()) {
      notes_.at(next.at(pitch)) = {
          .on = event.frame,
          .off = event.frame,
          .pitch = event.pitch,
          .velocity = event.velocity,
      };
    } else {
      notes_.at(next.at(pitch)++).off = event.frame;
    }
  }
}

const PlayedNote *ClipPlayback::across(std::size_t pitch,
                                       std::int64_t frame) const
    [[clang::nonblocking]] {
  // The indices come from first_, which never points past notes_.
  const auto begin =
      std::next(notes_.begin(),
                static_cast<std::ptrdiff_t>(first_.at(pitch % midiPitchCount)));
  const auto end = std::next(
      notes_.begin(),
      static_cast<std::ptrdiff_t>(first_.at((pitch % midiPitchCount) + 1)));
  // The last note that began before the frame.
  const auto after = std::partition_point(
      begin, end, [frame](const PlayedNote &note) { return note.on < frame; });
  if (after == begin) {
    return nullptr;
  }
  const PlayedNote &note = *std::prev(after);
  return note.off >= frame ? &note : nullptr;
}

std::size_t ClipPlayback::firstEventFrom(std::int64_t frame) const
    [[clang::nonblocking]] {
  const auto &events = events_.events;
  return static_cast<std::size_t>(
      std::ranges::lower_bound(events, frame, {}, &NoteEvent::frame) -
      events.begin());
}

std::size_t ClipPlayback::eventsIn(std::int64_t from, std::int64_t to) const
    [[clang::nonblocking]] {
  return firstEventFrom(to) - firstEventFrom(from);
}

TrackSequence::TrackSequence(std::uint64_t number, double sampleRate,
                             std::vector<SequencedClip> clips)
    : number_(number), sampleRate_(sampleRate), clips_(std::move(clips)) {
  std::erase_if(clips_, [](const SequencedClip &clip) {
    return clip.playback == nullptr || clip.playback->events().events.empty();
  });
  std::ranges::stable_sort(clips_, {}, [](const SequencedClip &clip) {
    return clip.playback->startFrame();
  });
}

std::size_t TrackSequence::firstEndingFrom(std::int64_t frame) const
    [[clang::nonblocking]] {
  const auto found =
      std::ranges::partition_point(clips_, [frame](const SequencedClip &clip) {
        return clip.playback->endFrame() < frame;
      });
  return static_cast<std::size_t>(found - clips_.begin());
}

const PlayedNote *TrackSequence::across(std::size_t pitch,
                                        std::int64_t frame) const
    [[clang::nonblocking]] {
  const std::size_t index = firstEndingFrom(frame);
  if (index >= clips_.size() ||
      clips_.at(index).playback->startFrame() >= frame) {
    return nullptr;
  }
  return clips_.at(index).playback->across(pitch, frame);
}

std::size_t TrackSequence::eventsIn(std::int64_t from, std::int64_t to) const
    [[clang::nonblocking]] {
  std::size_t count = 0;
  for (std::size_t index = firstEndingFrom(from);
       index < clips_.size() && clips_.at(index).playback->startFrame() < to;
       ++index) {
    count += clips_.at(index).playback->eventsIn(from, to);
  }
  return count;
}

std::size_t TrackSequence::densestStretch(std::int64_t frames) const {
  // The clips do not overlap, so their events one after the other are in
  // order of frame.
  std::vector<std::int64_t> all;
  for (const SequencedClip &clip : clips_) {
    for (const NoteEvent &event : clip.playback->events().events) {
      all.push_back(event.frame);
    }
  }
  std::size_t densest = 0;
  std::size_t first = 0;
  for (std::size_t last = 0; last < all.size(); ++last) {
    while (all.at(last) - all.at(first) >= frames) {
      ++first;
    }
    densest = std::max(densest, last - first + 1);
  }
  return densest;
}

} // namespace lina::engine::internal
