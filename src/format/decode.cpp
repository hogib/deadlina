// SPDX-License-Identifier: GPL-3.0-or-later
//
// From the parsed tree of format version 1 to a project, strictly. This file
// checks what the tree must look like and that every number fits the type the
// model keeps it in; whether the values make a valid project is for the
// model's `validate` to say, once, afterwards.
#include "internal/codec.hpp"
#include "internal/names.hpp"
#include "internal/strict_reader.hpp"
#include "internal/text_rules.hpp"
#include "lina/format/project_folder.hpp"

#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace lina::format::detail {

namespace {

constexpr std::int64_t int64Min = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t int64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t int32Min = std::numeric_limits<std::int32_t>::min();
constexpr std::int64_t int32Max = std::numeric_limits<std::int32_t>::max();
constexpr std::uint64_t uint64Max = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t uint32Max = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint64_t uint8Max = std::numeric_limits<std::uint8_t>::max();

class Decoder {
public:
  std::expected<Decoded, Error> project(const JsonValue &root);

private:
  // An identity that an object of the document carries itself: assigned,
  // below the counter, and not carried by any other object.
  std::uint64_t ownIdValue(const Node &node);
  template <model::ObjectKind Kind> model::Id<Kind> ownId(const Node &node) {
    return {ownIdValue(node)};
  }

  // A reference to another object's identity; the model checks that it leads
  // somewhere.
  template <model::ObjectKind Kind>
  model::Id<Kind> reference(const Node &node) {
    return {in_.unsignedInteger(node, uint64Max)};
  }

  std::int64_t ticks(const Node &node) {
    return in_.signedInteger(node, int64Min, int64Max);
  }
  model::TickPosition position(const Node &node) { return {ticks(node)}; }
  model::TickDuration duration(const Node &node) { return {ticks(node)}; }

  // Applies `readItem` to every element of an array node, stopping at the
  // first failure.
  template <class Item, class ReadItem>
  std::vector<Item> list(const Node &node, ReadItem readItem) {
    std::vector<Item> items;
    const JsonValue::Array &elements = in_.array(node);
    items.reserve(elements.size());
    std::size_t index = 0;
    for (const JsonValue &element : elements) {
      if (in_.failed()) {
        break;
      }
      items.push_back(readItem(elementNode(node, element, index)));
      ++index;
    }
    return items;
  }

  // The "type" member of an object that stands for a variant; empty after a
  // failure.
  std::string_view variantType(const Node &node);

  model::Tempo tempo(const Node &node);
  model::TimeSignature timeSignature(const Node &node);
  model::Colour colour(const Node &node);
  model::LoopRange projectLoop(const Node &node);
  model::LoopSettings clipLoop(const Node &node);
  model::MediaReference media(const Node &node);
  model::Scene scene(const Node &node);
  model::Note note(const Node &node);
  model::ControllerPoint controllerPoint(const Node &node);
  model::ControllerEnvelope controller(const Node &node);
  model::WarpMarker warpMarker(const Node &node);
  model::MidiContent midiContent(const Node &node);
  model::AudioContent audioContent(const Node &node);
  model::ClipContent content(const Node &node);
  model::Clip clip(const Node &node);
  model::Slot slot(const Node &node);
  model::TrackInput input(const Node &node);
  model::Send send(const Node &node);
  model::MixerStrip mixer(const Node &node);
  model::PluginDescriptor plugin(const Node &node);
  model::ParameterValue parameter(const Node &node);
  model::Device device(const Node &node);
  model::AutomationTarget target(const Node &node);
  model::AutomationPoint automationPoint(const Node &node);
  model::Envelope envelope(const Node &node);
  model::Track track(const Node &node);

  StrictReader in_;
  std::uint64_t nextId_{0};
  std::unordered_set<std::uint64_t> seen_;
};

std::uint64_t Decoder::ownIdValue(const Node &node) {
  const std::uint64_t value = in_.unsignedInteger(node, uint64Max);
  if (in_.failed()) {
    return 0;
  }
  if (value == 0 || value >= nextId_) {
    in_.fail(node, ErrorCode::Value,
             "the identity " + std::to_string(value) +
                 " is not one that \"next_id\" " + std::to_string(nextId_) +
                 " has handed out");
  } else if (!seen_.insert(value).second) {
    in_.fail(node, ErrorCode::Value,
             "the identity " + std::to_string(value) + " is used twice");
  }
  return value;
}

std::string_view Decoder::variantType(const Node &node) {
  if (in_.failed()) {
    return {};
  }
  const auto *object = std::get_if<JsonValue::Object>(&node.value->data);
  if (object == nullptr || findMember(*object, "type") == nullptr) {
    // Reports the wrong type, or the missing key.
    in_.expectObject(node, {"type"});
    return {};
  }
  return in_.string(member(node, "type"));
}

model::Tempo Decoder::tempo(const Node &node) {
  const double beatsPerMinute = in_.number(node);
  if (in_.failed()) {
    return {};
  }
  // The file holds beats per minute as a decimal; the model holds whole
  // hundredths. Only a value that is exactly a number of hundredths in range
  // is taken.
  const double scaled = std::nearbyint(beatsPerMinute * 100.0);
  if (scaled >= model::Tempo::minHundredths &&
      scaled <= model::Tempo::maxHundredths &&
      scaled / 100.0 == beatsPerMinute) {
    return model::Tempo::fromHundredths(static_cast<std::int32_t>(scaled))
        .value_or(model::Tempo{});
  }
  in_.fail(node, ErrorCode::Value,
           "the tempo must be from 20 to 300 in steps of 0.01");
  return {};
}

model::TimeSignature Decoder::timeSignature(const Node &node) {
  const JsonValue::Array &parts = in_.array(node);
  if (in_.failed()) {
    return {};
  }
  if (parts.size() != 2) {
    in_.fail(node, ErrorCode::Structure,
             "expected two numbers: beats in a bar, and the note that is one "
             "beat");
    return {};
  }
  const auto numerator = in_.signedInteger(elementNode(node, parts.front(), 0),
                                           int32Min, int32Max);
  const auto denominator =
      in_.signedInteger(elementNode(node, parts.back(), 1), int32Min, int32Max);
  const auto made =
      model::TimeSignature::make(static_cast<std::int32_t>(numerator),
                                 static_cast<std::int32_t>(denominator));
  if (!made.has_value()) {
    in_.fail(node, ErrorCode::Value,
             "the time signature must be 1 to 99 over 1, 2, 4, 8 or 16");
    return {};
  }
  return *made;
}

model::Colour Decoder::colour(const Node &node) {
  const std::string &text = in_.string(node);
  if (in_.failed()) {
    return {};
  }
  constexpr std::string_view digits = "0123456789abcdef";
  if (text.size() != 7 || text.front() != '#' ||
      text.find_first_not_of(digits, 1) != std::string::npos) {
    in_.fail(node, ErrorCode::Value,
             "a colour is \"#\" and six lower-case hexadecimal digits");
    return {};
  }
  return {static_cast<std::uint32_t>(std::stoul(text.substr(1), nullptr, 16))};
}

model::LoopRange Decoder::projectLoop(const Node &node) {
  model::LoopRange loop;
  if (in_.expectObject(node, {"enabled", "start", "length"})) {
    loop.enabled = in_.boolean(member(node, "enabled"));
    loop.start = position(member(node, "start"));
    loop.length = duration(member(node, "length"));
  }
  return loop;
}

model::LoopSettings Decoder::clipLoop(const Node &node) {
  const model::LoopRange loop = projectLoop(node);
  return {.enabled = loop.enabled, .start = loop.start, .length = loop.length};
}

model::MediaReference Decoder::media(const Node &node) {
  model::MediaReference made;
  if (!in_.expectObject(node, {"id", "path", "size", "hash", "sample_rate"})) {
    return made;
  }
  made.id = ownId<model::ObjectKind::Media>(member(node, "id"));
  const Node path = member(node, "path");
  made.path = in_.string(path);
  if (!in_.failed() && !isAllowedMediaPath(made.path)) {
    in_.fail(path, ErrorCode::Value,
             "a media path is either relative, inside the project folder and "
             "without empty, \".\" or \"..\" segments, or absolute");
  }
  made.sizeBytes = in_.unsignedInteger(member(node, "size"), uint64Max);
  const Node hash = member(node, "hash");
  made.contentHash = in_.string(hash);
  if (!in_.failed() && !isContentHash(made.contentHash)) {
    in_.fail(hash, ErrorCode::Value,
             "a content hash is 64 lower-case hexadecimal digits");
  }
  made.sampleRate = static_cast<std::uint32_t>(
      in_.unsignedInteger(member(node, "sample_rate"), uint32Max));
  return made;
}

model::Scene Decoder::scene(const Node &node) {
  model::Scene made;
  if (in_.expectObject(node, {"id", "name"})) {
    made.id = ownId<model::ObjectKind::Scene>(member(node, "id"));
    made.name = in_.string(member(node, "name"));
  }
  return made;
}

model::Note Decoder::note(const Node &node) {
  model::Note made;
  if (in_.expectObject(node, {"id", "start", "length", "pitch", "velocity"})) {
    made.id = ownId<model::ObjectKind::Note>(member(node, "id"));
    made.start = position(member(node, "start"));
    made.length = duration(member(node, "length"));
    made.pitch = static_cast<std::uint8_t>(
        in_.unsignedInteger(member(node, "pitch"), uint8Max));
    made.velocity = static_cast<std::uint8_t>(
        in_.unsignedInteger(member(node, "velocity"), uint8Max));
  }
  return made;
}

model::ControllerPoint Decoder::controllerPoint(const Node &node) {
  model::ControllerPoint made;
  if (in_.expectObject(node, {"time", "value"})) {
    made.time = position(member(node, "time"));
    made.value = static_cast<std::int32_t>(
        in_.signedInteger(member(node, "value"), int32Min, int32Max));
  }
  return made;
}

model::ControllerEnvelope Decoder::controller(const Node &node) {
  model::ControllerEnvelope made;
  if (in_.expectObject(node, {"kind", "controller", "points"})) {
    made.kind = in_.enumerator(member(node, "kind"), controllerKindNames);
    made.controller = static_cast<std::uint8_t>(
        in_.unsignedInteger(member(node, "controller"), uint8Max));
    made.points = list<model::ControllerPoint>(
        member(node, "points"),
        [this](const Node &point) { return controllerPoint(point); });
  }
  return made;
}

model::WarpMarker Decoder::warpMarker(const Node &node) {
  model::WarpMarker made;
  if (in_.expectObject(node, {"sample", "tick"})) {
    made.sample = in_.signedInteger(member(node, "sample"), int64Min, int64Max);
    made.tick = position(member(node, "tick"));
  }
  return made;
}

model::MidiContent Decoder::midiContent(const Node &node) {
  model::MidiContent made;
  if (in_.expectObject(node, {"type", "notes", "controllers"})) {
    made.notes = list<model::Note>(
        member(node, "notes"), [this](const Node &item) { return note(item); });
    made.controllers = list<model::ControllerEnvelope>(
        member(node, "controllers"),
        [this](const Node &item) { return controller(item); });
  }
  return made;
}

model::AudioContent Decoder::audioContent(const Node &node) {
  model::AudioContent made;
  if (!in_.expectObject(node, {
                                  "type",
                                  "media",
                                  "region",
                                  "gain_db",
                                  "follow_tempo",
                                  "stretch_mode",
                                  "transpose_cents",
                                  "warp",
                              })) {
    return made;
  }
  made.media = reference<model::ObjectKind::Media>(member(node, "media"));
  const Node region = member(node, "region");
  if (in_.expectObject(region, {"start", "end"})) {
    made.region.start =
        in_.signedInteger(member(region, "start"), int64Min, int64Max);
    made.region.end =
        in_.signedInteger(member(region, "end"), int64Min, int64Max);
  }
  made.gainDb = in_.number(member(node, "gain_db"));
  made.followTempo = in_.boolean(member(node, "follow_tempo"));
  made.stretchMode =
      in_.enumerator(member(node, "stretch_mode"), stretchModeNames);
  made.transposeCents = static_cast<std::int32_t>(
      in_.signedInteger(member(node, "transpose_cents"), int32Min, int32Max));
  const Node warp = member(node, "warp");
  if (in_.expectObject(warp, {"origin", "markers"})) {
    made.warp.origin =
        in_.enumerator(member(warp, "origin"), sourceTempoOriginNames);
    made.warp.markers = list<model::WarpMarker>(
        member(warp, "markers"),
        [this](const Node &item) { return warpMarker(item); });
  }
  return made;
}

model::ClipContent Decoder::content(const Node &node) {
  const std::string_view type = variantType(node);
  if (in_.failed()) {
    return {};
  }
  if (type == type_name::midi) {
    return midiContent(node);
  }
  if (type == type_name::audio) {
    return audioContent(node);
  }
  in_.fail(member(node, "type"), ErrorCode::Value,
           R"(the content of a clip is "midi" or "audio")");
  return {};
}

model::Clip Decoder::clip(const Node &node) {
  model::Clip made;
  if (!in_.expectObject(node, {
                                  "id",
                                  "name",
                                  "colour",
                                  "position",
                                  "length",
                                  "content_start",
                                  "loop",
                                  "launch_quantisation",
                                  "content",
                              })) {
    return made;
  }
  made.id = ownId<model::ObjectKind::Clip>(member(node, "id"));
  made.name = in_.string(member(node, "name"));
  made.colour = colour(member(node, "colour"));
  made.extent.position = position(member(node, "position"));
  made.extent.length = duration(member(node, "length"));
  made.extent.contentStart = position(member(node, "content_start"));
  made.loop = clipLoop(member(node, "loop"));
  const Node launch = member(node, "launch_quantisation");
  if (!isNull(launch)) {
    made.launchQuantisation = in_.enumerator(launch, launchQuantisationNames);
  }
  made.content = content(member(node, "content"));
  return made;
}

model::Slot Decoder::slot(const Node &node) {
  model::Slot made;
  if (in_.expectObject(node, {"id", "scene", "clip"})) {
    made.id = ownId<model::ObjectKind::Slot>(member(node, "id"));
    made.scene = reference<model::ObjectKind::Scene>(member(node, "scene"));
    const Node held = member(node, "clip");
    if (!isNull(held)) {
      made.clip = clip(held);
    }
  }
  return made;
}

model::TrackInput Decoder::input(const Node &node) {
  const std::string_view type = variantType(node);
  if (in_.failed()) {
    return {};
  }
  if (type == type_name::none) {
    in_.expectObject(node, {"type"});
    return model::NoInput{};
  }
  if (type == type_name::allMidi) {
    in_.expectObject(node, {"type"});
    return model::AllMidiInputs{};
  }
  if (type == type_name::audio) {
    model::AudioInput made;
    if (in_.expectObject(node, {"type", "channel", "stereo"})) {
      made.channel = static_cast<std::uint32_t>(
          in_.unsignedInteger(member(node, "channel"), uint32Max));
      made.stereo = in_.boolean(member(node, "stereo"));
    }
    return made;
  }
  if (type == type_name::midiDevice) {
    model::MidiDeviceInput made;
    if (in_.expectObject(node, {"type", "device"})) {
      made.device = in_.string(member(node, "device"));
    }
    return made;
  }
  in_.fail(member(node, "type"), ErrorCode::Value,
           R"(an input is "none", "audio", "all_midi" or "midi_device")");
  return {};
}

model::Send Decoder::send(const Node &node) {
  model::Send made;
  if (in_.expectObject(node, {"target", "level_db"})) {
    made.target = reference<model::ObjectKind::Track>(member(node, "target"));
    made.levelDb = in_.number(member(node, "level_db"));
  }
  return made;
}

model::MixerStrip Decoder::mixer(const Node &node) {
  model::MixerStrip made;
  if (!in_.expectObject(node, {
                                  "volume_db",
                                  "pan",
                                  "mute",
                                  "solo",
                                  "armed",
                                  "monitoring",
                                  "input",
                                  "sends",
                              })) {
    return made;
  }
  made.volumeDb = in_.number(member(node, "volume_db"));
  made.pan = in_.number(member(node, "pan"));
  made.mute = in_.boolean(member(node, "mute"));
  made.solo = in_.boolean(member(node, "solo"));
  made.armed = in_.boolean(member(node, "armed"));
  made.monitoring = in_.enumerator(member(node, "monitoring"), monitoringNames);
  made.input = input(member(node, "input"));
  made.sends = list<model::Send>(
      member(node, "sends"), [this](const Node &item) { return send(item); });
  return made;
}

model::PluginDescriptor Decoder::plugin(const Node &node) {
  model::PluginDescriptor made;
  if (in_.expectObject(node,
                       {"format", "identifier", "name", "vendor", "version"})) {
    made.format = in_.string(member(node, "format"));
    made.identifier = in_.string(member(node, "identifier"));
    made.name = in_.string(member(node, "name"));
    made.vendor = in_.string(member(node, "vendor"));
    made.version = in_.string(member(node, "version"));
  }
  return made;
}

model::ParameterValue Decoder::parameter(const Node &node) {
  model::ParameterValue made;
  if (in_.expectObject(node, {"id", "value"})) {
    made.id = in_.string(member(node, "id"));
    made.value = in_.number(member(node, "value"));
  }
  return made;
}

model::Device Decoder::device(const Node &node) {
  model::Device made;
  if (!in_.expectObject(
          node, {"id", "role", "plugin", "state", "bypassed", "parameters"})) {
    return made;
  }
  made.id = ownId<model::ObjectKind::Device>(member(node, "id"));
  made.role = in_.enumerator(member(node, "role"), deviceRoleNames);
  made.plugin = plugin(member(node, "plugin"));
  const Node state = member(node, "state");
  if (!isNull(state)) {
    made.stateRef = in_.string(state);
    if (!in_.failed() && !isStateFileName(made.stateRef)) {
      in_.fail(state, ErrorCode::Value,
               "a state reference is null or the name of a state file: 64 "
               "lower-case hexadecimal digits and \".bin\"");
    }
  }
  made.bypassed = in_.boolean(member(node, "bypassed"));
  made.parameters = list<model::ParameterValue>(
      member(node, "parameters"),
      [this](const Node &item) { return parameter(item); });
  return made;
}

model::AutomationTarget Decoder::target(const Node &node) {
  const std::string_view type = variantType(node);
  if (in_.failed()) {
    return {};
  }
  if (type == type_name::mixer) {
    model::MixerTarget made;
    if (in_.expectObject(node, {"type", "parameter"})) {
      made.parameter =
          in_.enumerator(member(node, "parameter"), mixerParameterNames);
    }
    return made;
  }
  if (type == type_name::send) {
    model::SendTarget made;
    if (in_.expectObject(node, {"type", "target"})) {
      made.target = reference<model::ObjectKind::Track>(member(node, "target"));
    }
    return made;
  }
  if (type == type_name::deviceParameter) {
    model::DeviceParameterTarget made;
    if (in_.expectObject(node, {"type", "device", "parameter"})) {
      made.device =
          reference<model::ObjectKind::Device>(member(node, "device"));
      made.parameter = in_.string(member(node, "parameter"));
    }
    return made;
  }
  in_.fail(member(node, "type"), ErrorCode::Value,
           R"(a target is "mixer", "send" or "device_parameter")");
  return {};
}

model::AutomationPoint Decoder::automationPoint(const Node &node) {
  model::AutomationPoint made;
  if (in_.expectObject(node, {"time", "value"})) {
    made.time = position(member(node, "time"));
    made.value = in_.number(member(node, "value"));
  }
  return made;
}

model::Envelope Decoder::envelope(const Node &node) {
  model::Envelope made;
  if (in_.expectObject(node, {"id", "target", "points"})) {
    made.id = ownId<model::ObjectKind::Envelope>(member(node, "id"));
    made.target = target(member(node, "target"));
    made.points = list<model::AutomationPoint>(
        member(node, "points"),
        [this](const Node &item) { return automationPoint(item); });
  }
  return made;
}

model::Track Decoder::track(const Node &node) {
  model::Track made;
  if (!in_.expectObject(node, {
                                  "id",
                                  "kind",
                                  "name",
                                  "colour",
                                  "mixer",
                                  "devices",
                                  "slots",
                                  "arrangement",
                                  "automation",
                              })) {
    return made;
  }
  made.id = ownId<model::ObjectKind::Track>(member(node, "id"));
  made.kind = in_.enumerator(member(node, "kind"), trackKindNames);
  made.name = in_.string(member(node, "name"));
  made.colour = colour(member(node, "colour"));
  made.mixer = mixer(member(node, "mixer"));
  made.devices =
      list<model::Device>(member(node, "devices"),
                          [this](const Node &item) { return device(item); });
  made.slots = list<model::Slot>(
      member(node, "slots"), [this](const Node &item) { return slot(item); });
  made.arrangement =
      list<model::Clip>(member(node, "arrangement"),
                        [this](const Node &item) { return clip(item); });
  made.automation = list<model::Envelope>(
      member(node, "automation"),
      [this](const Node &item) { return envelope(item); });
  return made;
}

std::expected<Decoded, Error> Decoder::project(const JsonValue &root) {
  const Node node = rootNode(root);
  Decoded decoded;
  model::Project &made = decoded.project;
  if (in_.expectObject(node, {
                                 "format",
                                 "written_by",
                                 "next_id",
                                 "tempo",
                                 "time_signature",
                                 "loop",
                                 "start_marker",
                                 "launch_quantisation",
                                 "media",
                                 "scenes",
                                 "tracks",
                                 "returns",
                                 "master",
                             })) {
    decoded.writtenBy = in_.string(member(node, "written_by"));
    nextId_ = in_.unsignedInteger(member(node, "next_id"), uint64Max);
    made.nextId = nextId_;
    made.tempo = tempo(member(node, "tempo"));
    made.timeSignature = timeSignature(member(node, "time_signature"));
    made.loop = projectLoop(member(node, "loop"));
    made.startMarker = position(member(node, "start_marker"));
    made.launchQuantisation = in_.enumerator(
        member(node, "launch_quantisation"), launchQuantisationNames);
    made.media = list<model::MediaReference>(
        member(node, "media"),
        [this](const Node &item) { return media(item); });
    made.scenes =
        list<model::Scene>(member(node, "scenes"),
                           [this](const Node &item) { return scene(item); });
    made.tracks =
        list<model::Track>(member(node, "tracks"),
                           [this](const Node &item) { return track(item); });
    made.returns =
        list<model::Track>(member(node, "returns"),
                           [this](const Node &item) { return track(item); });
    made.master = track(member(node, "master"));
  }
  if (auto error = in_.takeError()) {
    return std::unexpected(std::move(*error));
  }
  return decoded;
}

} // namespace

std::expected<Decoded, Error> decodeProject(const JsonValue &root) {
  return Decoder{}.project(root);
}

} // namespace lina::format::detail
