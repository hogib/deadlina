// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/documents.hpp"

#include "support/builders.hpp"
#include "support/generator.hpp"

#include "lina/format/sha256.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace lina::format::testing {

namespace {

using namespace lina::model; // NOLINT(google-build-using-namespace)
using model::testing::beat;
using model::testing::beats;
using model::testing::makeNote;

template <ObjectKind Kind>
Id<Kind> add(Document &document, const Command &command) {
  return mustApply(document, command).firstAdded<Kind>().value_or(Id<Kind>{});
}

void setTrack(Document &document, TrackId track, TrackProperty property) {
  mustApply(document,
            SetTrackProperty{.track = track, .property = std::move(property)});
}

void setClip(Document &document, ClipId clip, ClipProperty property) {
  mustApply(document,
            SetClipProperty{.clip = clip, .property = std::move(property)});
}

SlotId slotOf(const Document &document, TrackId track, SceneId scene) {
  const Track *owner = findTrack(document.project(), track);
  for (const Slot &slot : owner->slots) {
    if (slot.scene == scene) {
      return slot.id;
    }
  }
  return {};
}

Clip midiClip(std::string name, TickPosition position, TickDuration length,
              std::vector<Note> notes) {
  Clip clip = model::testing::makeMidiClip(position, length, std::move(notes));
  clip.name = std::move(name);
  return clip;
}

Device plugin(DeviceRole role, std::string name, std::string identifier) {
  Device device;
  device.role = role;
  device.plugin = {
      .format = "LV2",
      .identifier = std::move(identifier),
      .name = std::move(name),
      .vendor = "LINA tests",
      .version = "1.2.3",
  };
  return device;
}

Envelope envelope(AutomationTarget target,
                  std::vector<AutomationPoint> points) {
  return {
      .id = EnvelopeId{0},
      .target = std::move(target),
      .points = std::move(points),
  };
}

} // namespace

ChangeSet mustApply(Document &document, const Command &command) {
  auto changes = document.perform("build", command);
  if (!changes.has_value()) {
    std::cerr << "a command of a test document was refused: "
              << faultName(changes.error().fault) << " (command "
              << command.index() << ")\n";
    std::abort();
  }
  return std::move(*changes);
}

Document minimalDocument() {
  Document document;
  const SceneId scene =
      add<ObjectKind::Scene>(document, AddScene{.name = "Intro", .index = 0});
  const TrackId keys = add<ObjectKind::Track>(
      document, AddTrack{.kind = TrackKind::Midi, .name = "Keys", .index = 0});
  mustApply(document,
            AddClip{
                .container = slotOf(document, keys, scene),
                .clip = midiClip("two notes", TickPosition{0}, beats(4),
                                 {
                                     makeNote(60, beat(0), beats(1)),
                                     makeNote(64, beat(1), beats(2), 90),
                                 }),
            });
  return document;
}

std::string everythingStateContent() {
  return std::string{"opaque plugin state\0\x01\xff", 22};
}

Document everythingDocument() {
  Document document;
  const auto setProject = [&document](ProjectProperty property) {
    mustApply(document, SetProjectProperty{.property = property});
  };
  setProject(TempoValue{Tempo::fromHundredths(9'347).value_or(Tempo{})});
  setProject(
      TimeSignatureValue{TimeSignature::make(7, 8).value_or(TimeSignature{})});
  setProject(
      LoopRangeValue{{.enabled = true, .start = beat(14), .length = beats(7)}});
  setProject(StartMarkerValue{beat(3)});
  setProject(DefaultLaunchValue{LaunchQuantisation::Half});

  // Media: one copy inside the project, one file referenced where it is.
  const MediaId kick = add<ObjectKind::Media>(
      document, AddMedia{
                    {
                        .id = MediaId{0},
                        .path = "media/kick-3f9a1c2e.wav",
                        .sizeBytes = 352'844,
                        .contentHash = "3f9a1c2e5d0b4a6f8e7c9d1b2a3f4e5d6c7b8"
                                       "a9f0e1d2c3b4a5f6e7d8c9b0a1f",
                        .sampleRate = 44'100,
                    },
                });
  const MediaId snare = add<ObjectKind::Media>(
      document, AddMedia{
                    {
                        .id = MediaId{0},
                        .path = "/home/someone/samples/snare.flac",
                        .sizeBytes = 5'000'000'000,
                        .contentHash = "f0e1d2c3b4a5968778695a4b3c2d1e0ff0e1d2c"
                                       "3b4a5968778695a4b3c2d1e0f",
                        .sampleRate = 96'000,
                    },
                });

  const SceneId intro =
      add<ObjectKind::Scene>(document, AddScene{.name = "Intro", .index = 0});
  const SceneId verse = add<ObjectKind::Scene>(
      document, AddScene{
                    .name = "Verse \"two\" \\ caf\xC3\xA9 \xE2\x9C\x93\tend\n",
                    .index = 1,
                });
  add<ObjectKind::Scene>(document, AddScene{.name = "", .index = 2});

  const TrackId reverb = add<ObjectKind::Track>(
      document,
      AddTrack{.kind = TrackKind::Return, .name = "Reverb", .index = 0});
  const TrackId delay = add<ObjectKind::Track>(
      document,
      AddTrack{.kind = TrackKind::Return, .name = "Delay", .index = 1});
  const TrackId drums = add<ObjectKind::Track>(
      document,
      AddTrack{.kind = TrackKind::Audio, .name = "Drums", .index = 0});
  const TrackId keys = add<ObjectKind::Track>(
      document, AddTrack{.kind = TrackKind::Midi, .name = "Keys", .index = 1});
  const TrackId bass = add<ObjectKind::Track>(
      document, AddTrack{.kind = TrackKind::Midi, .name = "Bass", .index = 2});
  const TrackId vox = add<ObjectKind::Track>(
      document, AddTrack{.kind = TrackKind::Audio, .name = "Vox", .index = 3});
  const TrackId master = document.project().master.id;

  // Mixer strips and every kind of input.
  setTrack(document, drums, ColourValue{{0xFF8000}});
  setTrack(document, drums, VolumeValue{-6.5});
  setTrack(document, drums, PanValue{-0.25});
  setTrack(document, drums, ArmedValue{true});
  setTrack(document, drums, MonitoringValue{Monitoring::On});
  setTrack(document, drums,
           InputValue{AudioInput{.channel = 2, .stereo = true}});
  setTrack(document, drums, SendValue{{.target = reverb, .levelDb = -12.0}});
  setTrack(document, drums, SendValue{{.target = delay, .levelDb = -0.1}});
  setTrack(document, keys, InputValue{MidiDeviceInput{"Keystation 61"}});
  setTrack(document, keys, SoloValue{true});
  setTrack(document, bass, InputValue{AllMidiInputs{}});
  setTrack(document, bass, MuteValue{true});
  setTrack(document, bass, MonitoringValue{Monitoring::Off});
  setTrack(document, bass, VolumeValue{silenceDb});
  setTrack(document, vox, InputValue{NoInput{}});
  setTrack(document, vox, PanValue{1.0});
  setTrack(document, vox, VolumeValue{maxVolumeDb});
  setTrack(document, reverb, VolumeValue{-3.0});
  setTrack(document, reverb, ColourValue{{0x000001}});
  setTrack(document, delay, MuteValue{true});
  setTrack(document, master, VolumeValue{-1.5});

  // Devices: an instrument with parameters and stored state, effects on a
  // regular track, a return and the master.
  Device synth = plugin(DeviceRole::Instrument, "Test Synth", "urn:lina:synth");
  synth.stateRef = sha256Hex(everythingStateContent()) + ".bin";
  synth.parameters = {
      {.id = "cutoff", .value = 0.1},
      {.id = "resonance", .value = 1.0 / 3.0},
  };
  const DeviceId synthId = add<ObjectKind::Device>(
      document, AddDevice{.track = keys, .device = synth, .index = 0});
  Device chorus = plugin(DeviceRole::AudioEffect, "Chorus", "urn:lina:chorus");
  chorus.bypassed = true;
  add<ObjectKind::Device>(
      document, AddDevice{.track = keys, .device = chorus, .index = 1});
  const DeviceId gate = add<ObjectKind::Device>(
      document,
      AddDevice{
          .track = drums,
          .device = plugin(DeviceRole::AudioEffect, "Gate", "urn:lina:gate"),
          .index = 0,
      });
  mustApply(document, SetDeviceProperty{
                          .device = gate,
                          .property =
                              ParameterSettingValue{
                                  {.id = "threshold", .value = 0.000'001},
                              },
                      });
  add<ObjectKind::Device>(
      document,
      AddDevice{
          .track = reverb,
          .device = plugin(DeviceRole::AudioEffect, "Hall", "urn:lina:hall"),
          .index = 0,
      });
  add<ObjectKind::Device>(document,
                          AddDevice{
                              .track = master,
                              .device = plugin(DeviceRole::AudioEffect,
                                               "Limiter", "urn:lina:limiter"),
                              .index = 0,
                          });

  // An audio clip in a slot, with everything about it set.
  Clip kickLoop =
      model::testing::makeAudioClip(kick, TickPosition{0}, beats(8));
  kickLoop.name = "kick loop";
  const ClipId kickLoopId = add<ObjectKind::Clip>(
      document,
      AddClip{.container = slotOf(document, drums, intro), .clip = kickLoop});
  setClip(document, kickLoopId, ColourValue{{0x00FF7F}});
  setClip(document, kickLoopId, RegionValue{{.start = 441, .end = 352'800}});
  setClip(document, kickLoopId,
          WarpValue{
              {
                  .markers =
                      {
                          {.sample = 0, .tick = {}},
                          {.sample = 176'400, .tick = beat(4)},
                          {.sample = 352'800, .tick = beat(8)},
                      },
                  .origin = SourceTempoOrigin::Detected,
              },
          });
  setClip(document, kickLoopId, GainValue{-3.25});
  setClip(document, kickLoopId, StretchModeValue{StretchMode::Repitch});
  setClip(document, kickLoopId, TransposeValue{-1'200});
  setClip(document, kickLoopId,
          LoopValue{{.enabled = true, .start = beat(2), .length = beats(4)}});
  setClip(document, kickLoopId, LaunchOverrideValue{LaunchQuantisation::None});
  setClip(document, kickLoopId,
          ExtentValue{
              {
                  .position = TickPosition{0},
                  .length = beats(6),
                  .contentStart = beat(1),
              },
          });

  // Audio clips on the arrangement, one of them from the outside file and
  // not following the tempo.
  add<ObjectKind::Clip>(document, AddClip{
                                      .container = drums,
                                      .clip = model::testing::makeAudioClip(
                                          kick, beat(0), beats(4)),
                                  });
  Clip snareHit = model::testing::makeAudioClip(snare, beat(8), beats(2));
  snareHit.name = "snare hit";
  const ClipId snareHitId = add<ObjectKind::Clip>(
      document, AddClip{.container = drums, .clip = snareHit});
  setClip(document, snareHitId, FollowTempoValue{false});
  setClip(document, snareHitId,
          WarpValue{
              {
                  .markers =
                      {
                          {.sample = 0, .tick = {}},
                          {.sample = 96'000, .tick = beat(4)},
                      },
                  .origin = SourceTempoOrigin::Manual,
              },
          });
  setClip(document, snareHitId, GainValue{maxClipGainDb});

  // A MIDI clip in a slot, with notes and every kind of controller data.
  const ClipId chords = add<ObjectKind::Clip>(
      document, AddClip{
                    .container = slotOf(document, keys, verse),
                    .clip = midiClip("chords", TickPosition{0}, beats(8),
                                     {
                                         makeNote(60, beat(0), beats(2), 100),
                                         makeNote(64, beat(0), beats(2), 1),
                                         makeNote(67, beat(0), beats(2), 127),
                                         makeNote(0, beat(4), {1}, 64),
                                         makeNote(127, beat(4), beats(4), 64),
                                     }),
                });
  setClip(document, chords, LaunchOverrideValue{LaunchQuantisation::Bars8});
  const auto setController = [&document, chords](ControllerEnvelope data) {
    mustApply(document,
              SetControllerData{.clip = chords, .data = std::move(data)});
  };
  setController({
      .kind = ControllerKind::PitchBend,
      .controller = 0,
      .points =
          {
              {.time = beat(0), .value = -8'192},
              {.time = beat(1), .value = 0},
              {.time = beat(2), .value = 8'191},
          },
  });
  setController({
      .kind = ControllerKind::ChannelPressure,
      .controller = 0,
      .points = {{.time = beat(0), .value = 127}},
  });
  setController({
      .kind = ControllerKind::ControlChange,
      .controller = 1,
      .points =
          {
              {.time = beat(0), .value = 0},
              {.time = beat(0), .value = 64},
          },
  });
  setController({
      .kind = ControllerKind::ControlChange,
      .controller = 64,
      .points = {{.time = beat(7), .value = 127}},
  });

  // MIDI clips on the arrangement; the last one ends on the last tick a
  // document can hold.
  add<ObjectKind::Clip>(
      document, AddClip{
                    .container = bass,
                    .clip = midiClip("bass line", beat(4), beats(4),
                                     {
                                         makeNote(36, beat(0), beats(1)),
                                         makeNote(36, beat(1), beats(1)),
                                         makeNote(43, beat(2), beats(2)),
                                     }),
                });
  add<ObjectKind::Clip>(
      document, AddClip{
                    .container = bass,
                    .clip = midiClip("far away", {maxTicks - ticksPerWholeNote},
                                     {ticksPerWholeNote}, {}),
                });

  // Automation for every kind of target.
  mustApply(document, AddEnvelope{
                          .track = drums,
                          .envelope = envelope(
                              MixerTarget{MixerParameter::Volume},
                              {
                                  {.time = beat(0), .value = silenceDb},
                                  {.time = beat(4), .value = -6.0},
                                  {.time = beat(4), .value = 0.0},
                                  {.time = {maxTicks}, .value = maxVolumeDb},
                              }),
                      });
  mustApply(document,
            AddEnvelope{
                .track = drums,
                .envelope = envelope(MixerTarget{MixerParameter::Pan},
                                     {
                                         {.time = beat(0), .value = -1.0},
                                         {.time = beat(8), .value = 1.0},
                                     }),
            });
  mustApply(document,
            AddEnvelope{
                .track = drums,
                .envelope = envelope(SendTarget{reverb},
                                     {{.time = beat(2), .value = -24.5}}),
            });
  mustApply(
      document,
      AddEnvelope{
          .track = keys,
          .envelope = envelope(
              DeviceParameterTarget{.device = synthId, .parameter = "cutoff"},
              {
                  {.time = beat(0), .value = 0.0},
                  {.time = beat(16), .value = 0.123'456'789'012'345'67},
              }),
      });
  mustApply(document,
            AddEnvelope{
                .track = reverb,
                .envelope = envelope(MixerTarget{MixerParameter::Pan}, {}),
            });
  mustApply(document,
            AddEnvelope{
                .track = master,
                .envelope = envelope(MixerTarget{MixerParameter::Volume},
                                     {{.time = beat(32), .value = -96.0}}),
            });
  return document;
}

Document generatedDocument(std::uint64_t seed, int steps) {
  Document document;
  model::testing::CommandGenerator generator{seed};
  for (int step = 0; step < steps; ++step) {
    const Command command = generator.next(document.project());
    const auto ignored = document.perform("edit", command);
    static_cast<void>(ignored);
  }
  return document;
}

} // namespace lina::format::testing
