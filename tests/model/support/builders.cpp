// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/builders.hpp"

#include <algorithm>
#include <utility>

namespace lina::model::testing {

namespace {

// Applies a command that the fixture depends on. A failure here would leave
// identities unset, which the tests that use the fixture then report.
template <ObjectKind Kind>
Id<Kind> performAndFind(Document &document, const Command &command) {
  const auto changes = document.perform("fixture", command);
  if (!changes.has_value()) {
    return {};
  }
  return changes->firstAdded<Kind>().value_or(Id<Kind>{});
}

} // namespace

Note makeNote(std::uint8_t pitch, TickPosition start, TickDuration length,
              std::uint8_t velocity) {
  return {
      .id = NoteId{0},
      .pitch = pitch,
      .start = start,
      .length = length,
      .velocity = velocity,
  };
}

Clip makeMidiClip(TickPosition position, TickDuration length,
                  std::vector<Note> notes) {
  Clip clip;
  clip.name = "midi";
  clip.extent = {.position = position, .length = length, .contentStart = {}};
  clip.content = MidiContent{.notes = std::move(notes), .controllers = {}};
  return clip;
}

Clip makeAudioClip(MediaId media, TickPosition position, TickDuration length) {
  Clip clip;
  clip.name = "audio";
  clip.extent = {.position = position, .length = length, .contentStart = {}};
  AudioContent audio;
  audio.media = media;
  audio.region = {.start = 0, .end = 96'000};
  audio.warp.markers = {
      {.sample = 0, .tick = {}},
      {.sample = 96'000, .tick = beat(4)},
  };
  clip.content = std::move(audio);
  return clip;
}

MediaReference makeMedia(std::string path) {
  return {
      .id = MediaId{0},
      .path = std::move(path),
      .sizeBytes = 1'024,
      .contentHash =
          "00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff",
      .sampleRate = 48'000,
  };
}

Device makeEffect(std::string name) {
  Device device;
  device.role = DeviceRole::AudioEffect;
  device.plugin = {
      .format = "LV2",
      .identifier = "urn:test:" + name,
      .name = std::move(name),
      .vendor = "LINA tests",
      .version = "1",
  };
  return device;
}

Device makeInstrument(std::string name) {
  Device device = makeEffect(std::move(name));
  device.role = DeviceRole::Instrument;
  return device;
}

SlotId Fixture::slot(TrackId track, SceneId scene) const {
  const Track *owner = findTrack(project(), track);
  if (owner == nullptr) {
    return {};
  }
  const auto found = std::ranges::find(owner->slots, scene, &Slot::scene);
  return found == owner->slots.end() ? SlotId{} : found->id;
}

Fixture makeFixture() {
  Fixture fixture;
  Document &document = fixture.document;
  fixture.media = performAndFind<ObjectKind::Media>(
      document, AddMedia{makeMedia("media/loop.wav")});
  fixture.sceneA = performAndFind<ObjectKind::Scene>(
      document, AddScene{.name = "A", .index = 0});
  fixture.sceneB = performAndFind<ObjectKind::Scene>(
      document, AddScene{.name = "B", .index = 1});
  fixture.audio = performAndFind<ObjectKind::Track>(
      document,
      AddTrack{.kind = TrackKind::Audio, .name = "Audio", .index = 0});
  fixture.midi = performAndFind<ObjectKind::Track>(
      document, AddTrack{.kind = TrackKind::Midi, .name = "Midi", .index = 1});
  fixture.returnTrack = performAndFind<ObjectKind::Track>(
      document,
      AddTrack{.kind = TrackKind::Return, .name = "Return", .index = 0});
  return fixture;
}

} // namespace lina::model::testing
