// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/generator.hpp"

#include "support/builders.hpp"

#include <array>
#include <limits>
#include <string>
#include <utility>
#include <variant>

// Every draw from the engine is a statement of its own, or sits in a braced
// initialiser, whose elements are evaluated in order. Two draws as operands of
// one operator or as arguments of one call would come out in an order the
// compiler picks, and the sequence would differ between compilers.

namespace lina::model::testing {

namespace {

constexpr std::int64_t gridTicks = ticksPerQuarter / 2;

std::vector<const Track *> allTracks(const Project &project) {
  std::vector<const Track *> tracks;
  tracks.reserve(project.tracks.size() + project.returns.size() + 1);
  for (const Track &track : project.tracks) {
    tracks.push_back(&track);
  }
  for (const Track &track : project.returns) {
    tracks.push_back(&track);
  }
  tracks.push_back(&project.master);
  return tracks;
}

std::vector<const Clip *> allClips(const Project &project) {
  std::vector<const Clip *> clips;
  for (const Track &track : project.tracks) {
    for (const Slot &slot : track.slots) {
      if (slot.clip.has_value()) {
        clips.push_back(&*slot.clip);
      }
    }
    for (const Clip &clip : track.arrangement) {
      clips.push_back(&clip);
    }
  }
  return clips;
}

std::vector<const Device *> allDevices(const Project &project) {
  std::vector<const Device *> devices;
  for (const Track *track : allTracks(project)) {
    for (const Device &device : track->devices) {
      devices.push_back(&device);
    }
  }
  return devices;
}

std::vector<const Envelope *> allEnvelopes(const Project &project) {
  std::vector<const Envelope *> envelopes;
  for (const Track *track : allTracks(project)) {
    for (const Envelope &envelope : track->automation) {
      envelopes.push_back(&envelope);
    }
  }
  return envelopes;
}

// An identity the project has not handed out.
template <ObjectKind Kind> Id<Kind> unknownId(const Project &project) {
  return Id<Kind>{project.nextId + 7};
}

} // namespace

TickPosition CommandGenerator::position() {
  const std::uint64_t oddity = below(60);
  if (oddity == 0) {
    return {-1};
  }
  if (oddity == 1) {
    // At and around the last storable position.
    return {maxTicks - 2 + static_cast<std::int64_t>(below(5))};
  }
  return {static_cast<std::int64_t>(below(128)) * gridTicks};
}

TickDuration CommandGenerator::length() {
  const std::uint64_t oddity = below(60);
  if (oddity == 0) {
    return {0};
  }
  if (oddity == 1) {
    return {maxTicks - 2 + static_cast<std::int64_t>(below(5))};
  }
  return {static_cast<std::int64_t>(1 + below(8)) * gridTicks};
}

double CommandGenerator::level(double low, double high) {
  switch (below(60)) {
  case 0:
    return high + 1.0;
  case 1:
    return std::numeric_limits<double>::quiet_NaN();
  case 2:
    return std::numeric_limits<double>::infinity();
  case 3:
    return -0.0;
  case 4:
    return low;
  case 5:
    return high;
  default:
    return low + ((high - low) * static_cast<double>(below(1'001)) / 1'000.0);
  }
}

std::uint32_t CommandGenerator::colour() {
  // One in about 250 is past 0xFFFFFF.
  return static_cast<std::uint32_t>(below(0x1000000 + 0x10000));
}

std::size_t CommandGenerator::index(std::size_t bound, std::size_t overshoot) {
  const std::uint64_t picked = below(bound);
  return picked + (rarely(20) ? overshoot : 0);
}

TrackId CommandGenerator::anyTrack(const Project &project) {
  if (rarely(15)) {
    return unknownId<ObjectKind::Track>(project);
  }
  const std::vector<const Track *> tracks = allTracks(project);
  return tracks.at(below(tracks.size()))->id;
}

ClipId CommandGenerator::anyClip(const Project &project) {
  const std::vector<const Clip *> clips = allClips(project);
  if (clips.empty() || rarely(15)) {
    return unknownId<ObjectKind::Clip>(project);
  }
  return clips.at(below(clips.size()))->id;
}

std::vector<Note> CommandGenerator::notes(std::size_t count) {
  std::vector<Note> result;
  for (std::size_t i = 0; i < count; ++i) {
    const auto pitch =
        static_cast<std::uint8_t>(rarely(40) ? 200 : 58 + below(6));
    const auto startSteps = static_cast<std::int64_t>(below(32));
    const auto lengthSteps = static_cast<std::int64_t>(1 + below(4));
    const auto velocity = static_cast<std::uint8_t>(below(129));
    Note note = makeNote(pitch, TickPosition{startSteps * gridTicks / 2},
                         TickDuration{lengthSteps * gridTicks / 2}, velocity);
    if (rarely(60)) {
      note.start = position();
    }
    result.push_back(note);
  }
  return result;
}

Clip CommandGenerator::clip(const Project &project, TrackKind kind) {
  const bool midi = (kind == TrackKind::Midi) != rarely(20);
  const TickPosition at = position();
  const TickDuration span = length();
  Clip made;
  if (midi) {
    made = makeMidiClip(at, span, notes(below(4)));
    if (rarely(4)) {
      const auto value = static_cast<std::int32_t>(below(130));
      std::get<MidiContent>(made.content).controllers = {
          {
              .kind = ControllerKind::ControlChange,
              .controller = 1,
              .points = {{.time = TickPosition{0}, .value = value}},
          },
      };
    }
    if (rarely(30)) {
      // An identity where the document must assign one.
      std::get<MidiContent>(made.content)
          .notes.push_back({
              .id = NoteId{1},
              .pitch = 80,
              .start = TickPosition{0},
              .length = TickDuration{gridTicks},
              .velocity = 100,
          });
    }
  } else {
    const MediaId media =
        (project.media.empty() || rarely(20))
            ? unknownId<ObjectKind::Media>(project)
            : project.media.at(below(project.media.size())).id;
    made = makeAudioClip(media, at, span);
    if (rarely(4)) {
      std::get<AudioContent>(made.content).gainDb =
          level(silenceDb, maxClipGainDb);
    }
  }
  made.name = "clip " + std::to_string(named_++);
  if (rarely(30)) {
    made.id = ClipId{below(project.nextId + 3)};
  }
  if (rarely(30)) {
    made.colour.rgb = colour();
  }
  return made;
}

ClipContainer CommandGenerator::container(const Project &project) {
  if (rarely(20)) {
    return unknownId<ObjectKind::Slot>(project);
  }
  // Usually an audio or MIDI track; now and then a return track or the master,
  // which hold no clips, or a track that does not exist.
  if (project.tracks.empty() || rarely(20)) {
    return anyTrack(project);
  }
  const Track &track = project.tracks.at(below(project.tracks.size()));
  if (track.slots.empty() || below(2) == 0) {
    return track.id;
  }
  return track.slots.at(below(track.slots.size())).id;
}

TrackInput CommandGenerator::input() {
  switch (below(4)) {
  case 0:
    return NoInput{};
  case 1:
    return AudioInput{
        .channel = static_cast<std::uint32_t>(
            rarely(15) ? maxInputChannels - 1 + below(3) : below(8)),
        .stereo = below(2) == 0,
    };
  case 2:
    return AllMidiInputs{};
  default:
    return MidiDeviceInput{
        rarely(15) ? "" : "keys " + std::to_string(below(3)),
    };
  }
}

std::vector<AutomationPoint> CommandGenerator::points(double low, double high) {
  std::vector<AutomationPoint> made;
  const std::uint64_t count = below(5);
  TickPosition time{0};
  for (std::uint64_t i = 0; i < count; ++i) {
    // Mostly forwards in time, now and then standing still (a step), rarely
    // backwards (refused).
    const auto forward = static_cast<std::int64_t>(below(4));
    time = rarely(25) ? position() : time + TickDuration{forward * gridTicks};
    const double value = level(low, high);
    made.push_back({.time = time, .value = value});
  }
  return made;
}

Command CommandGenerator::projectCommand() {
  switch (below(5)) {
  case 0:
    return SetProjectProperty{
        TempoValue{
            Tempo::fromHundredths(
                static_cast<std::int32_t>(2'000 + below(28'001)))
                .value_or(Tempo{}),
        },
    };
  case 1: {
    constexpr std::array<std::int32_t, 5> denominators{1, 2, 4, 8, 16};
    const auto numerator = static_cast<std::int32_t>(1 + below(12));
    const std::int32_t denominator =
        denominators.at(below(denominators.size()));
    return SetProjectProperty{
        TimeSignatureValue{
            TimeSignature::make(numerator, denominator)
                .value_or(TimeSignature{}),
        },
    };
  }
  case 2:
    return SetProjectProperty{
        LoopRangeValue{
            {.enabled = below(2) == 0, .start = position(), .length = length()},
        },
    };
  case 3:
    return SetProjectProperty{StartMarkerValue{position()}};
  default:
    return SetProjectProperty{
        DefaultLaunchValue{enumerator<LaunchQuantisation>(10)},
    };
  }
}

Command CommandGenerator::mediaCommand(const Project &project) {
  const std::uint64_t choice = below(4);
  if (project.media.empty() || choice < 2) {
    const bool blank = rarely(20);
    std::string path;
    if (!blank) {
      const std::uint64_t number = named_++;
      path = options_.mediaPaths.empty()
                 ? "media/file" + std::to_string(number) + ".wav"
                 : options_.mediaPaths.at(number % options_.mediaPaths.size());
    }
    MediaReference media = makeMedia(std::move(path));
    if (rarely(20)) {
      media.id = MediaId{below(project.nextId + 3)};
    }
    return AddMedia{media};
  }
  const MediaId media = rarely(15)
                            ? unknownId<ObjectKind::Media>(project)
                            : project.media.at(below(project.media.size())).id;
  if (choice == 2) {
    return RemoveMedia{media};
  }
  const bool blank = rarely(15);
  std::string path;
  if (!blank) {
    const std::uint64_t number = named_++;
    path = options_.mediaPaths.empty()
               ? "elsewhere/" + std::to_string(number)
               : options_.mediaPaths.at(number % options_.mediaPaths.size());
  }
  return RelinkMedia{.media = media, .path = std::move(path)};
}

Command CommandGenerator::sceneCommand(const Project &project) {
  const std::size_t count = project.scenes.size();
  const std::uint64_t choice = below(5);
  if (count == 0 || choice < 2) {
    return AddScene{
        .name = "scene " + std::to_string(named_++),
        .index = index(count + 1, 3),
    };
  }
  const SceneId scene = rarely(15) ? unknownId<ObjectKind::Scene>(project)
                                   : project.scenes.at(below(count)).id;
  switch (choice) {
  case 2:
    return RemoveScene{scene};
  case 3:
    return MoveScene{.scene = scene, .index = index(count, count)};
  default:
    return RenameScene{
        .scene = scene,
        .name = "scene " + std::to_string(named_++),
    };
  }
}

Command CommandGenerator::trackCommand(const Project &project) {
  const std::uint64_t choice = below(6);
  if (choice < 3) {
    constexpr std::array<TrackKind, 8> kinds{
        TrackKind::Audio,  TrackKind::Audio,  TrackKind::Audio,
        TrackKind::Midi,   TrackKind::Midi,   TrackKind::Midi,
        TrackKind::Return, TrackKind::Master,
    };
    const TrackKind kind =
        rarely(40) ? enumerator<TrackKind>(4) : kinds.at(below(kinds.size()));
    const std::size_t count = kind == TrackKind::Return ? project.returns.size()
                                                        : project.tracks.size();
    return AddTrack{
        .kind = kind,
        .name = "track " + std::to_string(named_++),
        .index = index(count + 1, 3),
    };
  }
  const TrackId id = anyTrack(project);
  if (choice == 3) {
    return RemoveTrack{id};
  }
  const Track *track = findTrack(project, id);
  const std::size_t count =
      (track != nullptr && track->kind == TrackKind::Return)
          ? project.returns.size()
          : project.tracks.size();
  return MoveTrack{.track = id, .index = index(count + 1, count)};
}

Command CommandGenerator::mixerCommand(const Project &project) {
  const TrackId track = anyTrack(project);
  const auto set = [track](TrackProperty property) {
    return SetTrackProperty{.track = track, .property = std::move(property)};
  };
  switch (below(10)) {
  case 0:
    return set(NameValue{"track " + std::to_string(named_++)});
  case 1:
    return set(ColourValue{{colour()}});
  case 2:
    return set(VolumeValue{level(silenceDb, maxVolumeDb)});
  case 3:
    return set(PanValue{level(-1.0, 1.0)});
  case 4:
    return set(MuteValue{below(2) == 0});
  case 5:
    return set(SoloValue{below(2) == 0});
  case 6:
    return set(ArmedValue{below(2) == 0});
  case 7:
    return set(MonitoringValue{enumerator<Monitoring>(3)});
  case 8:
    return set(InputValue{input()});
  default: {
    const TrackId target =
        (project.returns.empty() || rarely(15))
            ? unknownId<ObjectKind::Track>(project)
            : project.returns.at(below(project.returns.size())).id;
    return set(SendValue{
        {.target = target, .levelDb = level(silenceDb, maxSendDb)},
    });
  }
  }
}

Command CommandGenerator::clipCommand(const Project &project) {
  if (project.tracks.empty()) {
    return trackCommand(project);
  }
  const std::uint64_t choice = below(6);
  if (allClips(project).empty() || choice < 3) {
    const ClipContainer destination = container(project);
    // The kind of clip follows the track the container is on, when it has one.
    const Track &guess = project.tracks.at(below(project.tracks.size()));
    TrackKind kind = guess.kind;
    if (const auto *slot = std::get_if<SlotId>(&destination)) {
      const SlotLocation location = findSlot(project, *slot);
      kind = location.track != nullptr ? location.track->kind : kind;
    } else {
      const Track *track = findTrack(project, std::get<TrackId>(destination));
      kind = track != nullptr ? track->kind : kind;
    }
    Clip made = clip(project, kind);
    if (std::holds_alternative<SlotId>(destination) && !rarely(20)) {
      made.extent.position = TickPosition{0};
    }
    return AddClip{.container = destination, .clip = std::move(made)};
  }
  const ClipId id = anyClip(project);
  if (choice == 3) {
    return RemoveClip{id};
  }
  const ClipContainer destination = container(project);
  const bool toSlot = std::holds_alternative<SlotId>(destination);
  return MoveClip{
      .clip = id,
      .container = destination,
      .position = (toSlot && !rarely(20)) ? TickPosition{0} : position(),
  };
}

Command CommandGenerator::clipPropertyCommand(const Project &project) {
  const ClipId id = anyClip(project);
  const ClipLocation location = findClip(project, id);
  const auto set = [id](ClipProperty property) {
    return SetClipProperty{.clip = id, .property = std::move(property)};
  };
  switch (below(11)) {
  case 0:
    return set(NameValue{"clip " + std::to_string(named_++)});
  case 1:
    return set(ColourValue{{colour()}});
  case 2: {
    ClipExtent extent;
    if (location.clip != nullptr) {
      extent = location.clip->extent;
    }
    // A clip in a slot keeps position zero unless a failure is wanted.
    if (location.slot == nullptr || rarely(20)) {
      extent.position = position();
    }
    extent.length = length();
    extent.contentStart = position();
    return set(ExtentValue{extent});
  }
  case 3:
    return set(LoopValue{
        {.enabled = below(2) == 0, .start = position(), .length = length()},
    });
  case 4:
    return set(LaunchOverrideValue{
        below(3) == 0 ? std::nullopt
                      : std::optional{enumerator<LaunchQuantisation>(10)},
    });
  case 5:
    return set(GainValue{level(silenceDb, maxClipGainDb)});
  case 6:
    return set(FollowTempoValue{below(2) == 0});
  case 7:
    return set(StretchModeValue{enumerator<StretchMode>(2)});
  case 8:
    return set(TransposeValue{static_cast<std::int32_t>(below(9'800)) - 4'850});
  case 9: {
    const auto end = static_cast<std::int64_t>(below(200'000));
    return set(WarpValue{
        {
            .markers =
                {
                    {.sample = 0, .tick = TickPosition{0}},
                    {.sample = end, .tick = TickPosition{gridTicks * 8}},
                },
            .origin = enumerator<SourceTempoOrigin>(4),
        },
    });
  }
  default:
    return set(RegionValue{
        {
            .start = static_cast<std::int64_t>(below(1'000)),
            .end = static_cast<std::int64_t>(below(100'000)),
        },
    });
  }
}

Command CommandGenerator::noteCommand(const Project &project) {
  const ClipId id = anyClip(project);
  const ClipLocation location = findClip(project, id);
  const MidiContent *midi =
      location.clip != nullptr
          ? std::get_if<MidiContent>(&location.clip->content)
          : nullptr;
  const std::uint64_t choice = below(4);
  if (midi == nullptr || midi->notes.empty() || choice < 2) {
    std::vector<Note> fresh = notes(1 + below(4));
    if (rarely(25)) {
      fresh.front().id = NoteId{below(project.nextId + 3)};
    }
    return AddNotes{.clip = id, .notes = std::move(fresh)};
  }
  const std::vector<Note> &existing = midi->notes;
  if (choice == 2) {
    RemoveNotes remove{.clip = id, .notes = {}};
    for (const Note &note : existing) {
      if (below(2) == 0) {
        remove.notes.push_back(note.id);
      }
    }
    if (rarely(15)) {
      remove.notes.push_back(unknownId<ObjectKind::Note>(project));
    }
    if (rarely(15)) {
      remove.notes.push_back(existing.front().id);
    }
    return remove;
  }
  SetNotes change{.clip = id, .notes = {}};
  for (const Note &note : existing) {
    if (below(2) == 0) {
      Note moved = notes(1).front();
      moved.id = note.id;
      change.notes.push_back(moved);
    }
  }
  if (rarely(15)) {
    Note stranger = existing.front();
    stranger.id = unknownId<ObjectKind::Note>(project);
    change.notes.push_back(stranger);
  }
  if (rarely(15)) {
    change.notes.push_back(existing.front());
  }
  return change;
}

Command CommandGenerator::controllerCommand(const Project &project) {
  const ClipId id = anyClip(project);
  ControllerEnvelope data;
  data.kind = enumerator<ControllerKind>(3);
  if (data.kind == ControllerKind::ControlChange) {
    // Few controller numbers, so that envelopes get replaced and removed.
    data.controller = static_cast<std::uint8_t>(rarely(20) ? 200 : below(3));
  }
  const bool bend = data.kind == ControllerKind::PitchBend;
  const std::uint64_t count = below(4);
  TickPosition time{0};
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto forward = static_cast<std::int64_t>(below(4));
    time = rarely(25) ? position() : time + TickDuration{forward * gridTicks};
    const auto raw = static_cast<std::int32_t>(below(bend ? 16'500 : 130));
    data.points.push_back({.time = time, .value = bend ? raw - 8'200 : raw});
  }
  return SetControllerData{.clip = id, .data = std::move(data)};
}

Device CommandGenerator::newDevice() {
  const std::uint64_t number = named_++;
  const std::string name = "plugin " + std::to_string(number);
  const bool instrument = below(4) == 0;
  const std::vector<Device> &palette =
      instrument ? options_.instruments : options_.effects;
  if (!palette.empty()) {
    return palette.at(number % palette.size());
  }
  return instrument ? makeInstrument(name) : makeEffect(name);
}

std::string CommandGenerator::parameterId() {
  if (rarely(20)) {
    return "";
  }
  const std::uint64_t number = below(4);
  return options_.parameterIds.empty()
             ? "p" + std::to_string(number)
             : options_.parameterIds.at(number % options_.parameterIds.size());
}

Command CommandGenerator::deviceCommand(const Project &project) {
  const std::vector<const Device *> devices = allDevices(project);
  const std::uint64_t choice = below(7);
  if (devices.empty() || choice < 3) {
    const TrackId track = anyTrack(project);
    const Track *found = findTrack(project, track);
    const std::size_t count = found != nullptr ? found->devices.size() : 0;
    Device device = newDevice();
    if (rarely(30)) {
      device.role = enumerator<DeviceRole>(2);
    }
    if (rarely(30)) {
      device.id = DeviceId{below(project.nextId + 3)};
    }
    if (rarely(6)) {
      device.parameters = {{.id = "p0", .value = level(0.0, 1.0)}};
    }
    return AddDevice{
        .track = track,
        .device = std::move(device),
        .index = index(count + 1, 2),
    };
  }
  const DeviceId id = rarely(15) ? unknownId<ObjectKind::Device>(project)
                                 : devices.at(below(devices.size()))->id;
  switch (choice) {
  case 3:
    return RemoveDevice{id};
  case 4: {
    // Inside its own chain.
    const DeviceLocation location = findDevice(project, id);
    const TrackId track = location.track != nullptr
                              ? location.track->id
                              : unknownId<ObjectKind::Track>(project);
    return MoveDevice{.device = id, .track = track, .index = below(3)};
  }
  case 5:
    // To any track, usually another one.
    return MoveDevice{
        .device = id,
        .track = anyTrack(project),
        .index = below(3),
    };
  default:
    break;
  }
  const auto set = [id](DeviceProperty property) {
    return SetDeviceProperty{.device = id, .property = std::move(property)};
  };
  switch (below(3)) {
  case 0:
    return set(BypassedValue{below(2) == 0});
  case 1:
    // The name of a state file: a content hash, here one of four made-up
    // ones, and ".bin".
    return set(
        StateValue{std::string(63, '0') + std::to_string(below(4)) + ".bin"});
  default: {
    std::string parameter = parameterId();
    return set(ParameterSettingValue{
        {
            .id = std::move(parameter),
            .value =
                below(3) == 0 ? std::nullopt : std::optional{level(0.0, 1.0)},
        },
    });
  }
  }
}

Command CommandGenerator::automationCommand(const Project &project) {
  const std::vector<const Envelope *> envelopes = allEnvelopes(project);
  const std::uint64_t choice = below(6);
  if (envelopes.empty() || choice < 3) {
    const TrackId trackId = anyTrack(project);
    const Track *track = findTrack(project, trackId);
    Envelope envelope;
    const std::uint64_t sort = below(4);
    if (sort == 0) {
      envelope.target = MixerTarget{MixerParameter::Volume};
      envelope.points = points(silenceDb, maxVolumeDb);
    } else if (sort == 1) {
      envelope.target = MixerTarget{enumerator<MixerParameter>(2)};
      envelope.points = points(-1.0, 1.0);
    } else if (sort == 2) {
      const TrackId target =
          (project.returns.empty() || rarely(15))
              ? unknownId<ObjectKind::Track>(project)
              : project.returns.at(below(project.returns.size())).id;
      envelope.target = SendTarget{target};
      envelope.points = points(silenceDb, maxSendDb);
    } else {
      const DeviceId device =
          (track == nullptr || track->devices.empty() || rarely(15))
              ? unknownId<ObjectKind::Device>(project)
              : track->devices.at(below(track->devices.size())).id;
      envelope.target = DeviceParameterTarget{
          .device = device,
          .parameter = rarely(20) ? "" : "p" + std::to_string(below(2)),
      };
      envelope.points = points(0.0, 1.0);
    }
    if (rarely(25)) {
      envelope.id = EnvelopeId{below(project.nextId + 3)};
    }
    return AddEnvelope{.track = trackId, .envelope = std::move(envelope)};
  }
  const EnvelopeId id = rarely(15) ? unknownId<ObjectKind::Envelope>(project)
                                   : envelopes.at(below(envelopes.size()))->id;
  if (choice == 3) {
    return RemoveEnvelope{id};
  }
  // Points in the range of a plugin parameter suit every target but pan's
  // left half; the others are refused often enough to matter.
  return SetEnvelopePoints{
      .envelope = id,
      .points = below(2) == 0 ? points(0.0, 1.0) : points(silenceDb, 0.0),
  };
}

Command CommandGenerator::next(const Project &project) {
  const std::uint64_t choice = below(33);
  if (choice < 1) {
    return projectCommand();
  }
  if (choice < 2) {
    return mediaCommand(project);
  }
  if (choice < 4) {
    return sceneCommand(project);
  }
  if (choice < 7) {
    return trackCommand(project);
  }
  if (choice < 10) {
    return mixerCommand(project);
  }
  if (choice < 16) {
    return clipCommand(project);
  }
  if (choice < 19) {
    return clipPropertyCommand(project);
  }
  if (choice < 23) {
    return noteCommand(project);
  }
  if (choice < 25) {
    return controllerCommand(project);
  }
  if (choice < 29) {
    return deviceCommand(project);
  }
  return automationCommand(project);
}

} // namespace lina::model::testing
