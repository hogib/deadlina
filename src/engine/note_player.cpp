// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/note_player.hpp"

namespace lina::engine::internal {

namespace te = tracktion;

namespace {

std::uint64_t numberOf(const TrackSequence *sequence) [[clang::nonblocking]] {
  return sequence == nullptr ? 0 : sequence->number();
}

void noteOff(te::MidiMessageArray &out, std::size_t pitch, double frameInBlock,
             double sampleRate) [[clang::nonblocking]] {
  out.addMidiMessage(
      juce::MidiMessage::noteOff(sequencerMidiChannel, static_cast<int>(pitch)),
      (frameInBlock + noteOffOffsetFrames) / sampleRate, te::MPESourceID{});
}

void noteOn(te::MidiMessageArray &out, std::size_t pitch, std::uint8_t velocity,
            double frameInBlock, double sampleRate) [[clang::nonblocking]] {
  out.addMidiMessage(
      juce::MidiMessage::noteOn(sequencerMidiChannel, static_cast<int>(pitch),
                                juce::uint8{velocity}),
      (frameInBlock + noteOnOffsetFrames) / sampleRate, te::MPESourceID{});
}

} // namespace

void NotePlayer::reset() [[clang::nonblocking]] {
  sounding_.fill(false);
  expectedFrame_ = nowhere;
  playingNumber_ = 0;
}

void NotePlayer::forgetAll() [[clang::nonblocking]] { sounding_.fill(false); }

std::size_t NotePlayer::soundingCount() const [[clang::nonblocking]] {
  std::size_t count = 0;
  for (const bool sounding : sounding_) {
    count += sounding ? 1 : 0;
  }
  return count;
}

bool NotePlayer::mustCheck(const TrackSequence *sequence,
                           const SequencerBlock &block) const
    [[clang::nonblocking]] {
  return block.start != expectedFrame_ || numberOf(sequence) != playingNumber_;
}

std::size_t NotePlayer::bound(const TrackSequence *sequence,
                              const SequencerBlock &block) const
    [[clang::nonblocking]] {
  if (!block.playing) {
    return soundingCount();
  }
  std::size_t count = 0;
  if (block.starting || mustCheck(sequence, block)) {
    count += soundingCount();
  }
  if (sequence != nullptr) {
    count += block.starting ? midiPitchCount : 0;
    count += sequence->eventsIn(block.start, block.start + block.length);
  }
  return count;
}

void NotePlayer::play(const TrackSequence *sequence,
                      const SequencerBlock &block, te::MidiMessageArray &out,
                      double sampleRate) [[clang::nonblocking]] {
  if (!block.playing) {
    endAll(out, sampleRate);
    moveOn(sequence, block);
    return;
  }
  if (block.starting) {
    endAll(out, sampleRate);
    if (sequence != nullptr) {
      chase(*sequence, block.start, out, sampleRate);
    }
  } else if (mustCheck(sequence, block)) {
    endUnheld(sequence, block.start, out, sampleRate);
  }
  if (sequence != nullptr) {
    playEvents(*sequence, block, out, sampleRate);
  }
  moveOn(sequence, block);
}

void NotePlayer::skip(const TrackSequence *sequence,
                      const SequencerBlock &block) [[clang::nonblocking]] {
  sounding_.fill(false);
  moveOn(sequence, block);
}

void NotePlayer::moveOn(const TrackSequence *sequence,
                        const SequencerBlock &block) [[clang::nonblocking]] {
  expectedFrame_ = block.playing ? block.start + block.length : nowhere;
  playingNumber_ = numberOf(sequence);
}

void NotePlayer::endAll(te::MidiMessageArray &out, double sampleRate)
    [[clang::nonblocking]] {
  for (std::size_t pitch = 0; pitch < midiPitchCount; ++pitch) {
    // The index is below the size of the array, so at() never throws.
    if (sounding_.at(pitch)) {
      noteOff(out, pitch, 0.0, sampleRate);
      sounding_.at(pitch) = false;
    }
  }
}

void NotePlayer::endUnheld(const TrackSequence *sequence, std::int64_t frame,
                           te::MidiMessageArray &out, double sampleRate)
    [[clang::nonblocking]] {
  for (std::size_t pitch = 0; pitch < midiPitchCount; ++pitch) {
    if (sounding_.at(pitch) &&
        (sequence == nullptr || sequence->across(pitch, frame) == nullptr)) {
      noteOff(out, pitch, 0.0, sampleRate);
      sounding_.at(pitch) = false;
    }
  }
}

void NotePlayer::chase(const TrackSequence &sequence, std::int64_t frame,
                       te::MidiMessageArray &out, double sampleRate)
    [[clang::nonblocking]] {
  for (std::size_t pitch = 0; pitch < midiPitchCount; ++pitch) {
    // A note that ends on the frame is over; one that starts on it is
    // played with the block's events.
    const PlayedNote *note = sequence.across(pitch, frame);
    if (note != nullptr && note->off > frame) {
      noteOn(out, pitch, note->velocity, 0.0, sampleRate);
      sounding_.at(pitch) = true;
    }
  }
}

void NotePlayer::playEvents(const TrackSequence &sequence,
                            const SequencerBlock &block,
                            te::MidiMessageArray &out, double sampleRate)
    [[clang::nonblocking]] {
  const std::int64_t end = block.start + block.length;
  const auto &clips = sequence.clips();
  for (std::size_t index = sequence.firstEndingFrom(block.start);
       index < clips.size() && clips.at(index).playback->startFrame() < end;
       ++index) {
    const ClipPlayback &clip = *clips.at(index).playback;
    const auto &events = clip.events().events;
    for (std::size_t at = clip.firstEventFrom(block.start);
         at < events.size() && events.at(at).frame < end; ++at) {
      const NoteEvent &event = events.at(at);
      const std::size_t pitch = event.pitch % midiPitchCount;
      const auto frameInBlock = static_cast<double>(event.frame - block.start);
      if (event.isNoteOn()) {
        noteOn(out, pitch, event.velocity, frameInBlock, sampleRate);
        sounding_.at(pitch) = true;
      } else if (sounding_.at(pitch)) {
        noteOff(out, pitch, frameInBlock, sampleRate);
        sounding_.at(pitch) = false;
      }
    }
  }
}

} // namespace lina::engine::internal
