// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules.hpp"

#include "lookup.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <variant>

namespace lina::model {

namespace detail {

namespace {

constexpr std::uint8_t maxMidiValue = 127;
constexpr std::int32_t minPitchBend = -8'192;
constexpr std::int32_t maxPitchBend = 8'191;
constexpr std::uint32_t maxColour = 0xFFFFFF;

void require(bool holds, Fault fault, ObjectRef object, Failures &failures) {
  if (!holds) {
    failures.push_back({.fault = fault, .object = object});
  }
}

// True when an enumeration value is no greater than its last enumerator. The
// enumerations of the model all count up from zero without gaps.
template <class Enumeration> bool upTo(Enumeration value, Enumeration last) {
  return static_cast<unsigned>(value) <= static_cast<unsigned>(last);
}

// What a lead byte of UTF-8 announces: how many continuation bytes follow and
// the smallest and largest value the first of them may have (Unicode, table
// 3-7). Nothing follows a byte that cannot start a sequence.
struct Utf8Sequence {
  std::size_t following;
  std::uint8_t firstLow;
  std::uint8_t firstHigh;
};

constexpr std::uint8_t continuationLow = 0x80;
constexpr std::uint8_t continuationHigh = 0xBF;

Utf8Sequence sequenceFor(std::uint8_t lead) {
  if (lead >= 0xC2 && lead <= 0xDF) {
    return {
        .following = 1,
        .firstLow = continuationLow,
        .firstHigh = continuationHigh,
    };
  }
  if (lead >= 0xE0 && lead <= 0xEF) {
    // E0 would otherwise allow overlong forms, ED the surrogates.
    return {
        .following = 2,
        .firstLow = lead == 0xE0 ? std::uint8_t{0xA0} : continuationLow,
        .firstHigh = lead == 0xED ? std::uint8_t{0x9F} : continuationHigh,
    };
  }
  if (lead >= 0xF0 && lead <= 0xF4) {
    // F0 would otherwise allow overlong forms, F4 values past U+10FFFF.
    return {
        .following = 3,
        .firstLow = lead == 0xF0 ? std::uint8_t{0x90} : continuationLow,
        .firstHigh = lead == 0xF4 ? std::uint8_t{0x8F} : continuationHigh,
    };
  }
  return {.following = 0, .firstLow = 0, .firstHigh = 0};
}

constexpr std::size_t contentHashDigits = 64;
constexpr std::string_view stateSuffix = ".bin";

void requireText(std::string_view text, ObjectRef object, Failures &failures) {
  require(isStorableText(text), Fault::TextInvalid, object, failures);
}

bool noteValuesAllowed(const Note &note) {
  return note.pitch <= maxMidiValue && note.velocity >= 1 &&
         note.velocity <= maxMidiValue && isStorable(note.start, note.length);
}

auto noteOrderKey(const Note &note) {
  return std::tuple{note.start, note.pitch};
}

auto controllerOrderKey(const ControllerEnvelope &envelope) {
  return std::tuple{envelope.kind, envelope.controller};
}

bool controllerValueAllowed(ControllerKind kind, std::int32_t value) {
  if (kind == ControllerKind::PitchBend) {
    return value >= minPitchBend && value <= maxPitchBend;
  }
  return value >= 0 && value <= std::int32_t{maxMidiValue};
}

bool extentStorable(const ClipExtent &extent) {
  return isStorable(extent.position, extent.length) &&
         isStorable(extent.contentStart);
}

void checkArrangementOrder(const std::vector<Clip> &arrangement,
                           Failures &failures) {
  const Clip *previous = nullptr;
  for (const Clip &clip : arrangement) {
    if (!extentStorable(clip.extent)) {
      // Reported by checkClipHeader; an end computed from it would mean
      // nothing.
      previous = nullptr;
      continue;
    }
    if (previous != nullptr) {
      require(previous->extent.position <= clip.extent.position,
              Fault::OrderViolation, ObjectRef{clip.id}, failures);
      require(previous->end() <= clip.extent.position, Fault::ClipOverlap,
              ObjectRef{clip.id}, failures);
    }
    previous = &clip;
  }
}

void checkParameters(const Device &device, Failures &failures) {
  const ObjectRef self{device.id};
  const bool valuesAllowed = std::ranges::all_of(
      device.parameters, [](const ParameterValue &parameter) {
        return !parameter.id.empty() && inRange(parameter.value, 0.0, 1.0);
      });
  require(valuesAllowed, Fault::ValueOutOfRange, self, failures);
  const bool textsAllowed = std::ranges::all_of(
      device.parameters, [](const ParameterValue &parameter) {
        return isStorableText(parameter.id);
      });
  require(textsAllowed, Fault::TextInvalid, self, failures);
  const bool sortedAndUnique =
      std::ranges::adjacent_find(
          device.parameters,
          [](const ParameterValue &a, const ParameterValue &b) {
            return a.id >= b.id;
          }) == device.parameters.end();
  require(sortedAndUnique, Fault::OrderViolation, self, failures);
}

// What is wrong with an input on a track of this kind, if anything.
std::optional<Fault> inputFault(TrackKind kind, const NoInput & /*input*/) {
  return kind == TrackKind::Midi ? std::optional{Fault::WrongInputKind}
                                 : std::nullopt;
}

std::optional<Fault> inputFault(TrackKind kind, const AudioInput &input) {
  if (kind != TrackKind::Audio) {
    return Fault::WrongInputKind;
  }
  const std::uint32_t channels = input.stereo ? 2 : 1;
  return (input.channel <= maxInputChannels - channels)
             ? std::nullopt
             : std::optional{Fault::ValueOutOfRange};
}

std::optional<Fault> inputFault(TrackKind kind,
                                const AllMidiInputs & /*input*/) {
  return kind == TrackKind::Midi ? std::nullopt
                                 : std::optional{Fault::WrongInputKind};
}

std::optional<Fault> inputFault(TrackKind kind, const MidiDeviceInput &input) {
  if (kind != TrackKind::Midi) {
    return Fault::WrongInputKind;
  }
  if (!isStorableText(input.device)) {
    return Fault::TextInvalid;
  }
  return input.device.empty() ? std::optional{Fault::ValueOutOfRange}
                              : std::nullopt;
}

void checkMixer(const Project &project, const Track &track,
                Failures &failures) {
  const ObjectRef self{track.id};
  const MixerStrip &mixer = track.mixer;
  require(inRange(mixer.volumeDb, silenceDb, maxVolumeDb) &&
              inRange(mixer.pan, -1.0, 1.0) && isKnown(mixer.monitoring),
          Fault::ValueOutOfRange, self, failures);
  const std::optional<Fault> wrongInput = std::visit(
      [&track](const auto &input) { return inputFault(track.kind, input); },
      mixer.input);
  if (wrongInput.has_value()) {
    failures.push_back({.fault = *wrongInput, .object = self});
  }

  if (isRegular(track.kind)) {
    const bool matchReturns = std::ranges::equal(mixer.sends, project.returns,
                                                 {}, &Send::target, &Track::id);
    require(matchReturns, Fault::SendsMismatch, self, failures);
    const bool levelsAllowed =
        std::ranges::all_of(mixer.sends, [](const Send &send) {
          return inRange(send.levelDb, silenceDb, maxSendDb);
        });
    require(levelsAllowed, Fault::ValueOutOfRange, self, failures);
    return;
  }

  require(mixer.sends.empty(), Fault::SendsMismatch, self, failures);
  require(!mixer.armed && mixer.monitoring == Monitoring::Auto,
          Fault::WrongTrackKind, self, failures);
  if (track.kind == TrackKind::Master) {
    require(mixer.pan == 0.0 && !mixer.mute && !mixer.solo,
            Fault::WrongTrackKind, self, failures);
  }
}

// The range of values a target takes.
struct ValueRange {
  double low{0.0};
  double high{1.0};
};

// The range of an envelope's target on this track, or nothing when the track
// has no such parameter.
std::optional<ValueRange> targetRange(const Project & /*project*/,
                                      const Track &track,
                                      const MixerTarget &target) {
  if (target.parameter == MixerParameter::Volume) {
    return ValueRange{.low = silenceDb, .high = maxVolumeDb};
  }
  const bool hasPan = target.parameter == MixerParameter::Pan &&
                      track.kind != TrackKind::Master;
  return hasPan ? std::optional{ValueRange{.low = -1.0, .high = 1.0}}
                : std::nullopt;
}

std::optional<ValueRange> targetRange(const Project &project,
                                      const Track &track,
                                      const SendTarget &target) {
  const bool hasSend = isRegular(track.kind) &&
                       findById(project.returns, target.target) != nullptr;
  return hasSend
             ? std::optional{ValueRange{.low = silenceDb, .high = maxSendDb}}
             : std::nullopt;
}

std::optional<ValueRange> targetRange(const Project & /*project*/,
                                      const Track &track,
                                      const DeviceParameterTarget &target) {
  const bool hasParameter = !target.parameter.empty() &&
                            isStorableText(target.parameter) &&
                            findById(track.devices, target.device) != nullptr;
  return hasParameter ? std::optional{ValueRange{}} : std::nullopt;
}

void checkEnvelopes(const Project &project, const Track &track,
                    Failures &failures) {
  const Envelope *previous = nullptr;
  for (auto envelope = track.automation.begin();
       envelope != track.automation.end(); ++envelope) {
    checkEnvelope(project, track, *envelope, failures);
    const bool firstForTarget = std::ranges::none_of(
        track.automation.begin(), envelope, [&](const Envelope &earlier) {
          return earlier.target == envelope->target;
        });
    require(firstForTarget, Fault::EnvelopeTarget, ObjectRef{envelope->id},
            failures);
    require(previous == nullptr || previous->id < envelope->id,
            Fault::OrderViolation, ObjectRef{envelope->id}, failures);
    previous = &*envelope;
  }
}

void checkSlots(const Project &project, const Track &track,
                Failures &failures) {
  const ObjectRef self{track.id};
  if (!isRegular(track.kind)) {
    require(track.slots.empty(), Fault::SlotsMismatch, self, failures);
    require(track.arrangement.empty(), Fault::WrongTrackKind, self, failures);
    return;
  }
  const bool matchScenes = std::ranges::equal(track.slots, project.scenes, {},
                                              &Slot::scene, &Scene::id);
  require(matchScenes, Fault::SlotsMismatch, self, failures);
}

void checkIds(const Project &project, Failures &failures) {
  std::vector<ObjectRef> ids;
  auto collect = [&ids](ObjectRef object, ObjectRef /*parent*/) {
    ids.push_back(object);
  };
  walkProject(project, collect);
  require(project.nextId <= maxNextId, Fault::IdsExhausted, projectRef,
          failures);
  std::ranges::sort(ids, {}, &ObjectRef::id);
  const ObjectRef *previous = nullptr;
  for (const ObjectRef &object : ids) {
    require(object.id != 0 && object.id < project.nextId, Fault::IdInvalid,
            object, failures);
    require(previous == nullptr || previous->id != object.id, Fault::IdInvalid,
            object, failures);
    previous = &object;
  }
}

} // namespace

bool isRegular(TrackKind kind) {
  return kind == TrackKind::Audio || kind == TrackKind::Midi;
}

bool isStorableText(std::string_view text) {
  std::size_t following = 0;
  std::uint8_t low = continuationLow;
  std::uint8_t high = continuationHigh;
  for (const char character : text) {
    const auto byte = static_cast<std::uint8_t>(character);
    if (following > 0) {
      if (byte < low || byte > high) {
        return false;
      }
      --following;
      low = continuationLow;
      high = continuationHigh;
      continue;
    }
    if (byte == 0) {
      return false;
    }
    if (byte < 0x80) {
      continue;
    }
    const Utf8Sequence sequence = sequenceFor(byte);
    if (sequence.following == 0) {
      return false;
    }
    following = sequence.following;
    low = sequence.firstLow;
    high = sequence.firstHigh;
  }
  return following == 0;
}

bool isContentHash(std::string_view text) {
  constexpr std::string_view digits = "0123456789abcdef";
  return text.size() == contentHashDigits &&
         text.find_first_not_of(digits) == std::string_view::npos;
}

bool isStateRef(std::string_view text) {
  return text.empty() ||
         (text.ends_with(stateSuffix) &&
          isContentHash(text.substr(0, text.size() - stateSuffix.size())));
}

bool idsAvailable(const Project &project, std::uint64_t count) {
  return project.nextId <= maxNextId && count <= maxNextId - project.nextId;
}

bool inRange(double value, double low, double high) {
  return value >= low && value <= high &&
         (value != 0.0 || !std::signbit(value));
}

bool isKnown(TrackKind value) { return upTo(value, TrackKind::Master); }
bool isKnown(Monitoring value) { return upTo(value, Monitoring::Auto); }
bool isKnown(LaunchQuantisation value) {
  return upTo(value, LaunchQuantisation::ThirtySecond);
}
bool isKnown(StretchMode value) { return upTo(value, StretchMode::Repitch); }
bool isKnown(SourceTempoOrigin value) {
  return upTo(value, SourceTempoOrigin::Manual);
}
bool isKnown(ControllerKind value) {
  return upTo(value, ControllerKind::ControlChange);
}
bool isKnown(DeviceRole value) { return upTo(value, DeviceRole::AudioEffect); }
bool isKnown(MixerParameter value) { return upTo(value, MixerParameter::Pan); }

bool noteBefore(const Note &a, const Note &b) {
  return noteOrderKey(a) < noteOrderKey(b);
}

void sortNotes(std::vector<Note> &notes) {
  std::ranges::sort(notes, noteBefore);
}

void checkNotes(const std::vector<Note> &notes, Failures &failures) {
  std::array<TickPosition, maxMidiValue + 1> lastEnd{};
  const Note *previous = nullptr;
  for (const Note &note : notes) {
    const ObjectRef self{note.id};
    require(previous == nullptr || !noteBefore(note, *previous),
            Fault::OrderViolation, self, failures);
    previous = &note;
    if (!noteValuesAllowed(note)) {
      failures.push_back({.fault = Fault::ValueOutOfRange, .object = self});
      continue;
    }
    TickPosition &end = lastEnd.at(note.pitch);
    require(note.start >= end, Fault::NoteOverlap, self, failures);
    end = std::max(end, note.start + note.length);
  }
}

void checkControllers(const std::vector<ControllerEnvelope> &controllers,
                      ObjectRef clip, Failures &failures) {
  const ControllerEnvelope *previous = nullptr;
  for (const ControllerEnvelope &envelope : controllers) {
    const bool numberAllowed = envelope.kind == ControllerKind::ControlChange
                                   ? envelope.controller <= maxMidiValue
                                   : envelope.controller == 0;
    require(isKnown(envelope.kind) && numberAllowed, Fault::ValueOutOfRange,
            clip, failures);
    require(previous == nullptr ||
                controllerOrderKey(*previous) < controllerOrderKey(envelope),
            Fault::OrderViolation, clip, failures);
    previous = &envelope;

    // An envelope without points is not kept: no data is no envelope.
    const bool pointsAllowed =
        !envelope.points.empty() &&
        std::ranges::all_of(envelope.points, [&](const ControllerPoint &point) {
          return isStorable(point.time) &&
                 controllerValueAllowed(envelope.kind, point.value);
        });
    require(pointsAllowed, Fault::ValueOutOfRange, clip, failures);
    require(std::ranges::is_sorted(envelope.points, {}, &ControllerPoint::time),
            Fault::OrderViolation, clip, failures);
  }
}

void checkAudio(const Project &project, const AudioContent &audio,
                ObjectRef clip, Failures &failures) {
  require(findById(project.media, audio.media) != nullptr, Fault::MediaMissing,
          clip, failures);
  require(audio.region.start >= 0 && audio.region.start < audio.region.end,
          Fault::ValueOutOfRange, clip, failures);
  require(inRange(audio.gainDb, silenceDb, maxClipGainDb),
          Fault::ValueOutOfRange, clip, failures);
  require(audio.transposeCents >= -maxTransposeCents &&
              audio.transposeCents <= maxTransposeCents,
          Fault::ValueOutOfRange, clip, failures);
  require(isKnown(audio.stretchMode) && isKnown(audio.warp.origin),
          Fault::ValueOutOfRange, clip, failures);

  const std::vector<WarpMarker> &markers = audio.warp.markers;
  const bool markersAllowed =
      markers.size() >= 2 &&
      std::ranges::all_of(markers, [](const WarpMarker &marker) {
        return marker.sample >= 0 && isStorable(marker.tick);
      });
  require(markersAllowed, Fault::ValueOutOfRange, clip, failures);
  const bool increasing =
      std::ranges::adjacent_find(
          markers, [](const WarpMarker &a, const WarpMarker &b) {
            return a.sample >= b.sample || a.tick >= b.tick;
          }) == markers.end();
  require(increasing, Fault::OrderViolation, clip, failures);
}

void checkClipHeader(const Clip &clip, TrackKind trackKind, bool inSlot,
                     Failures &failures) {
  const ObjectRef self{clip.id};
  require(extentStorable(clip.extent), Fault::ValueOutOfRange, self, failures);
  require(!inSlot || clip.extent.position == TickPosition{0},
          Fault::ValueOutOfRange, self, failures);
  require(isStorable(clip.loop.start, clip.loop.length), Fault::ValueOutOfRange,
          self, failures);
  require(clip.colour.rgb <= maxColour, Fault::ValueOutOfRange, self, failures);
  requireText(clip.name, self, failures);
  require(!clip.launchQuantisation.has_value() ||
              isKnown(*clip.launchQuantisation),
          Fault::ValueOutOfRange, self, failures);
  const TrackKind wanted = std::holds_alternative<MidiContent>(clip.content)
                               ? TrackKind::Midi
                               : TrackKind::Audio;
  require(trackKind == wanted, Fault::WrongTrackKind, self, failures);
}

void checkClip(const Project &project, const Clip &clip, TrackKind trackKind,
               bool inSlot, Failures &failures) {
  const ObjectRef self{clip.id};
  checkClipHeader(clip, trackKind, inSlot, failures);
  if (const auto *midi = std::get_if<MidiContent>(&clip.content)) {
    checkNotes(midi->notes, failures);
    checkControllers(midi->controllers, self, failures);
  } else {
    checkAudio(project, std::get<AudioContent>(clip.content), self, failures);
  }
}

bool overlapsOther(const std::vector<Clip> &arrangement,
                   const ClipExtent &extent, ClipId self) {
  const TickPosition end = extent.position + extent.length;
  return std::ranges::any_of(arrangement, [&](const Clip &other) {
    return other.id != self && other.extent.position < end &&
           extent.position < other.end();
  });
}

void sortArrangement(std::vector<Clip> &arrangement) {
  std::ranges::sort(arrangement, {},
                    [](const Clip &clip) { return clip.extent.position; });
}

void checkDevices(TrackKind trackKind, const std::vector<Device> &devices,
                  ObjectRef track, Failures &failures) {
  for (auto device = devices.begin(); device != devices.end(); ++device) {
    const ObjectRef self{device->id};
    require(isKnown(device->role) && isStateRef(device->stateRef),
            Fault::ValueOutOfRange, self, failures);
    const PluginDescriptor &plugin = device->plugin;
    require(isStorableText(plugin.format) &&
                isStorableText(plugin.identifier) &&
                isStorableText(plugin.name) && isStorableText(plugin.vendor) &&
                isStorableText(plugin.version),
            Fault::TextInvalid, self, failures);
    const bool placementAllowed =
        device->role != DeviceRole::Instrument ||
        (trackKind == TrackKind::Midi && device == devices.begin());
    require(placementAllowed, Fault::DeviceChainOrder, track, failures);
    checkParameters(*device, failures);
  }
}

void checkTrackHeader(const Project &project, const Track &track,
                      Failures &failures) {
  require(isKnown(track.kind) && track.colour.rgb <= maxColour,
          Fault::ValueOutOfRange, ObjectRef{track.id}, failures);
  requireText(track.name, ObjectRef{track.id}, failures);
  checkMixer(project, track, failures);
}

void checkEnvelope(const Project &project, const Track &track,
                   const Envelope &envelope, Failures &failures) {
  const ObjectRef self{envelope.id};
  const auto *mixerTarget = std::get_if<MixerTarget>(&envelope.target);
  require(mixerTarget == nullptr || isKnown(mixerTarget->parameter),
          Fault::ValueOutOfRange, self, failures);
  const std::optional<ValueRange> range = std::visit(
      [&](const auto &target) { return targetRange(project, track, target); },
      envelope.target);
  if (!range.has_value()) {
    failures.push_back({.fault = Fault::EnvelopeTarget, .object = self});
    return;
  }
  const bool pointsAllowed = std::ranges::all_of(
      envelope.points, [&range](const AutomationPoint &point) {
        return isStorable(point.time) &&
               inRange(point.value, range->low, range->high);
      });
  require(pointsAllowed, Fault::ValueOutOfRange, self, failures);
  require(std::ranges::is_sorted(envelope.points, {}, &AutomationPoint::time),
          Fault::OrderViolation, self, failures);
}

void checkTrack(const Project &project, const Track &track,
                Failures &failures) {
  checkTrackHeader(project, track, failures);
  checkSlots(project, track, failures);
  for (const Slot &slot : track.slots) {
    if (slot.clip.has_value()) {
      checkClip(project, *slot.clip, track.kind, true, failures);
    }
  }
  for (const Clip &clip : track.arrangement) {
    checkClip(project, clip, track.kind, false, failures);
  }
  checkArrangementOrder(track.arrangement, failures);
  checkDevices(track.kind, track.devices, ObjectRef{track.id}, failures);
  checkEnvelopes(project, track, failures);
}

void checkMedia(const MediaReference &media, Failures &failures) {
  require(!media.path.empty() && media.sampleRate >= 1 &&
              isContentHash(media.contentHash),
          Fault::ValueOutOfRange, ObjectRef{media.id}, failures);
  requireText(media.path, ObjectRef{media.id}, failures);
}

void checkScene(const Scene &scene, Failures &failures) {
  requireText(scene.name, ObjectRef{scene.id}, failures);
}

void checkLoopRange(const LoopRange &loop, Failures &failures) {
  require(isStorable(loop.start, loop.length), Fault::ValueOutOfRange,
          projectRef, failures);
}

void checkStartMarker(TickPosition position, Failures &failures) {
  require(isStorable(position), Fault::ValueOutOfRange, projectRef, failures);
}

void checkLaunchQuantisation(LaunchQuantisation quantisation,
                             Failures &failures) {
  require(isKnown(quantisation), Fault::ValueOutOfRange, projectRef, failures);
}

} // namespace detail

std::vector<Failure> validate(const Project &project) {
  using detail::require;
  detail::Failures failures;
  detail::checkIds(project, failures);
  detail::checkLoopRange(project.loop, failures);
  detail::checkStartMarker(project.startMarker, failures);
  detail::checkLaunchQuantisation(project.launchQuantisation, failures);
  for (const MediaReference &media : project.media) {
    detail::checkMedia(media, failures);
  }
  for (const Scene &scene : project.scenes) {
    detail::checkScene(scene, failures);
  }

  for (const Track &track : project.tracks) {
    require(detail::isRegular(track.kind), Fault::WrongTrackKind,
            ObjectRef{track.id}, failures);
    detail::checkTrack(project, track, failures);
  }
  for (const Track &track : project.returns) {
    require(track.kind == TrackKind::Return, Fault::WrongTrackKind,
            ObjectRef{track.id}, failures);
    detail::checkTrack(project, track, failures);
  }
  require(project.master.kind == TrackKind::Master, Fault::WrongTrackKind,
          ObjectRef{project.master.id}, failures);
  detail::checkTrack(project, project.master, failures);
  return failures;
}

} // namespace lina::model
