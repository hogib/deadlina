// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/midi_events.hpp"

#include <algorithm>
#include <array>
#include <optional>

namespace lina::engine::internal {

namespace {

constexpr std::size_t pitchCount = 128;

/// A note as it will sound: [on, off) in frames.
struct Sounding {
  std::int64_t on{0};
  std::int64_t off{0};
  std::uint8_t pitch{0};
  std::uint8_t velocity{0};
  bool dropped{false};
};

/// Collects the notes of a clip stretch by stretch.
class Unroller {
public:
  Unroller(const MidiClipContent &clip, const TimeMap &time, ClipEvents &result)
      : clip_(&clip), time_(&time), result_(&result),
        clipStart_(clip.extent.position.ticks),
        clipLength_(clip.extent.length.ticks) {}

  /// Plays the content [from, to) starting `offset` ticks into the clip.
  /// False when nothing after this stretch can sound.
  bool play(std::int64_t from, std::int64_t to, std::int64_t offset) {
    const std::int64_t stretchEnd =
        clipStart_ + std::min(clipLength_, offset + (to - from));
    const auto &notes = clip_->notes;
    auto note = std::ranges::lower_bound(
        notes, from, {}, [](const auto &n) { return n.start.ticks; });
    for (; note != notes.end() && note->start.ticks < to; ++note) {
      const std::int64_t onTick =
          clipStart_ + offset + (note->start.ticks - from);
      if (onTick >= stretchEnd) {
        break;
      }
      const std::int64_t on = time_->frame(onTick);
      if (on >= result_->endFrame) {
        return false;
      }
      if (sounding_.size() >= maxSoundingNotesPerClip) {
        result_->truncated = true;
        return false;
      }
      const std::int64_t offTick =
          std::min(onTick + note->length.ticks, stretchEnd);
      sounding_.push_back({
          .on = on,
          .off = std::min(time_->frame(offTick), result_->endFrame),
          .pitch = note->pitch,
          .velocity = note->velocity,
          .dropped = false,
      });
    }
    return true;
  }

  /// True when a note starts inside the content range [from, to).
  [[nodiscard]] bool anyNoteStartsIn(std::int64_t from, std::int64_t to) const {
    const auto &notes = clip_->notes;
    const auto note = std::ranges::lower_bound(
        notes, from, {}, [](const auto &n) { return n.start.ticks; });
    return note != notes.end() && note->start.ticks < to;
  }

  [[nodiscard]] std::vector<Sounding> &sounding() { return sounding_; }

private:
  const MidiClipContent *clip_;
  const TimeMap *time_;
  ClipEvents *result_;
  std::int64_t clipStart_;
  std::int64_t clipLength_;
  std::vector<Sounding> sounding_;
};

void unroll(const MidiClipContent &clip, Unroller &unroller) {
  const std::int64_t length = clip.extent.length.ticks;
  const std::int64_t contentStart = clip.extent.contentStart.ticks;
  const std::int64_t loopStart = clip.loop.start.ticks;
  const std::int64_t loopEnd = loopStart + clip.loop.length.ticks;
  if (!clip.loop.enabled || contentStart >= loopEnd) {
    unroller.play(contentStart, contentStart + length, 0);
    return;
  }
  if (!unroller.play(contentStart, loopEnd, 0) ||
      !unroller.anyNoteStartsIn(loopStart, loopEnd)) {
    return;
  }
  for (std::int64_t offset = loopEnd - contentStart; offset < length;
       offset += loopEnd - loopStart) {
    if (!unroller.play(loopStart, loopEnd, offset)) {
      return;
    }
  }
}

/// Gives every note at least one frame and ends a note where the next one of
/// its pitch starts. The notes are in order of their note-on frames.
void separate(std::vector<Sounding> &sounding) {
  std::array<std::optional<std::size_t>, pitchCount> last{};
  for (std::size_t index = 0; index < sounding.size(); ++index) {
    Sounding &note = sounding.at(index);
    note.off = std::max(note.off, note.on + 1);
    auto &previousOfPitch = last.at(note.pitch % pitchCount);
    if (previousOfPitch.has_value()) {
      Sounding &previous = sounding.at(*previousOfPitch);
      previous.off = std::min(previous.off, note.on);
      previous.dropped = previous.off <= previous.on;
    }
    previousOfPitch = index;
  }
}

} // namespace

ClipEvents noteEvents(const MidiClipContent &clip, const TimeMap &time) {
  ClipEvents result;
  const std::int64_t clipStart = clip.extent.position.ticks;
  result.startFrame = time.frame(clipStart);
  result.endFrame = std::min(time.frame(clipStart + clip.extent.length.ticks),
                             time.horizonFrame());
  if (result.startFrame >= result.endFrame) {
    result.endFrame = result.startFrame;
    return result;
  }

  Unroller unroller(clip, time, result);
  unroll(clip, unroller);
  auto &sounding = unroller.sounding();
  separate(sounding);

  result.events.reserve(2 * sounding.size());
  for (const Sounding &note : sounding) {
    if (!note.dropped) {
      result.events.push_back(
          {.frame = note.on, .pitch = note.pitch, .velocity = note.velocity});
      result.events.push_back(
          {.frame = note.off, .pitch = note.pitch, .velocity = 0});
    }
  }
  // By frame, and stable. Within a frame the events then stand as they were
  // made, note by note, which puts every note-off before every note-on: a
  // note that ends on a frame began before it, and so comes before every
  // note that begins there.
  std::ranges::stable_sort(result.events, {}, &NoteEvent::frame);
  return result;
}

} // namespace lina::engine::internal
