// SPDX-License-Identifier: GPL-3.0-or-later
//
// From a project to the canonical text of format version 1. The order of the
// keys of every object is the order of the calls below, and lists are written
// in the order the model keeps them, which the model defines
// (docs/design/16-project-file-format-encoding.md).
#include "internal/codec.hpp"
#include "internal/guard.hpp"
#include "internal/json_writer.hpp"
#include "internal/names.hpp"
#include "internal/text_rules.hpp"
#include "lina/format/document_io.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <utility>
#include <variant>

namespace lina::format::detail {

namespace {

using model::ObjectRef;

void writeColour(JsonWriter &out, model::Colour colour) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string text = "#";
  for (int shift = 20; shift >= 0; shift -= 4) {
    text += digits.at((colour.rgb >> static_cast<unsigned>(shift)) & 0xFU);
  }
  out.string(text);
}

void writeLoop(JsonWriter &out, bool enabled, model::TickPosition start,
               model::TickDuration length) {
  out.beginObject(Layout::Inline);
  out.key("enabled");
  out.boolean(enabled);
  out.key("start");
  out.signedInteger(start.ticks);
  out.key("length");
  out.signedInteger(length.ticks);
  out.endObject();
}

void writeLaunchQuantisation(JsonWriter &out,
                             model::LaunchQuantisation quantisation) {
  out.string(nameOf(launchQuantisationNames, quantisation));
}

void writeMedia(JsonWriter &out, const model::MediaReference &media) {
  out.beginObject(Layout::Block);
  out.key("id");
  out.unsignedInteger(media.id.value);
  out.key("path");
  out.string(media.path);
  out.key("size");
  out.unsignedInteger(media.sizeBytes);
  out.key("hash");
  out.string(media.contentHash);
  out.key("sample_rate");
  out.unsignedInteger(media.sampleRate);
  out.endObject();
}

void writeScene(JsonWriter &out, const model::Scene &scene) {
  out.beginObject(Layout::Inline);
  out.key("id");
  out.unsignedInteger(scene.id.value);
  out.key("name");
  out.string(scene.name);
  out.endObject();
}

void writeNote(JsonWriter &out, const model::Note &note) {
  out.beginObject(Layout::Inline);
  out.key("id");
  out.unsignedInteger(note.id.value);
  out.key("start");
  out.signedInteger(note.start.ticks);
  out.key("length");
  out.signedInteger(note.length.ticks);
  out.key("pitch");
  out.unsignedInteger(note.pitch);
  out.key("velocity");
  out.unsignedInteger(note.velocity);
  out.endObject();
}

void writeController(JsonWriter &out,
                     const model::ControllerEnvelope &envelope) {
  out.beginObject(Layout::Block);
  out.key("kind");
  out.string(nameOf(controllerKindNames, envelope.kind));
  out.key("controller");
  out.unsignedInteger(envelope.controller);
  out.key("points");
  out.beginArray(Layout::Block);
  for (const model::ControllerPoint &point : envelope.points) {
    out.beginObject(Layout::Inline);
    out.key("time");
    out.signedInteger(point.time.ticks);
    out.key("value");
    out.signedInteger(point.value);
    out.endObject();
  }
  out.endArray();
  out.endObject();
}

void writeContent(JsonWriter &out, const model::MidiContent &midi) {
  out.beginObject(Layout::Block);
  out.key("type");
  out.string(type_name::midi);
  out.key("notes");
  out.beginArray(Layout::Block);
  for (const model::Note &note : midi.notes) {
    writeNote(out, note);
  }
  out.endArray();
  out.key("controllers");
  out.beginArray(Layout::Block);
  for (const model::ControllerEnvelope &envelope : midi.controllers) {
    writeController(out, envelope);
  }
  out.endArray();
  out.endObject();
}

void writeContent(JsonWriter &out, const model::AudioContent &audio) {
  out.beginObject(Layout::Block);
  out.key("type");
  out.string(type_name::audio);
  out.key("media");
  out.unsignedInteger(audio.media.value);
  out.key("region");
  out.beginObject(Layout::Inline);
  out.key("start");
  out.signedInteger(audio.region.start);
  out.key("end");
  out.signedInteger(audio.region.end);
  out.endObject();
  out.key("gain_db");
  out.number(audio.gainDb);
  out.key("follow_tempo");
  out.boolean(audio.followTempo);
  out.key("stretch_mode");
  out.string(nameOf(stretchModeNames, audio.stretchMode));
  out.key("transpose_cents");
  out.signedInteger(audio.transposeCents);
  out.key("warp");
  out.beginObject(Layout::Block);
  out.key("origin");
  out.string(nameOf(sourceTempoOriginNames, audio.warp.origin));
  out.key("markers");
  out.beginArray(Layout::Block);
  for (const model::WarpMarker &marker : audio.warp.markers) {
    out.beginObject(Layout::Inline);
    out.key("sample");
    out.signedInteger(marker.sample);
    out.key("tick");
    out.signedInteger(marker.tick.ticks);
    out.endObject();
  }
  out.endArray();
  out.endObject();
  out.endObject();
}

void writeClip(JsonWriter &out, const model::Clip &clip) {
  out.beginObject(Layout::Block);
  out.key("id");
  out.unsignedInteger(clip.id.value);
  out.key("name");
  out.string(clip.name);
  out.key("colour");
  writeColour(out, clip.colour);
  out.key("position");
  out.signedInteger(clip.extent.position.ticks);
  out.key("length");
  out.signedInteger(clip.extent.length.ticks);
  out.key("content_start");
  out.signedInteger(clip.extent.contentStart.ticks);
  out.key("loop");
  writeLoop(out, clip.loop.enabled, clip.loop.start, clip.loop.length);
  out.key("launch_quantisation");
  if (clip.launchQuantisation.has_value()) {
    writeLaunchQuantisation(out, *clip.launchQuantisation);
  } else {
    out.null();
  }
  out.key("content");
  std::visit([&out](const auto &content) { writeContent(out, content); },
             clip.content);
  out.endObject();
}

// The members of an input after its "type".
void writeInputFields(JsonWriter &out, const model::NoInput & /*input*/) {
  out.string(type_name::none);
}

void writeInputFields(JsonWriter &out, const model::AudioInput &input) {
  out.string(type_name::audio);
  out.key("channel");
  out.unsignedInteger(input.channel);
  out.key("stereo");
  out.boolean(input.stereo);
}

void writeInputFields(JsonWriter &out, const model::AllMidiInputs & /*input*/) {
  out.string(type_name::allMidi);
}

void writeInputFields(JsonWriter &out, const model::MidiDeviceInput &input) {
  out.string(type_name::midiDevice);
  out.key("device");
  out.string(input.device);
}

void writeInput(JsonWriter &out, const model::TrackInput &input) {
  out.beginObject(Layout::Inline);
  out.key("type");
  std::visit([&out](const auto &held) { writeInputFields(out, held); }, input);
  out.endObject();
}

void writeMixer(JsonWriter &out, const model::MixerStrip &mixer) {
  out.beginObject(Layout::Block);
  out.key("volume_db");
  out.number(mixer.volumeDb);
  out.key("pan");
  out.number(mixer.pan);
  out.key("mute");
  out.boolean(mixer.mute);
  out.key("solo");
  out.boolean(mixer.solo);
  out.key("armed");
  out.boolean(mixer.armed);
  out.key("monitoring");
  out.string(nameOf(monitoringNames, mixer.monitoring));
  out.key("input");
  writeInput(out, mixer.input);
  out.key("sends");
  out.beginArray(Layout::Block);
  for (const model::Send &send : mixer.sends) {
    out.beginObject(Layout::Inline);
    out.key("target");
    out.unsignedInteger(send.target.value);
    out.key("level_db");
    out.number(send.levelDb);
    out.endObject();
  }
  out.endArray();
  out.endObject();
}

void writeDevice(JsonWriter &out, const model::Device &device) {
  out.beginObject(Layout::Block);
  out.key("id");
  out.unsignedInteger(device.id.value);
  out.key("role");
  out.string(nameOf(deviceRoleNames, device.role));
  out.key("plugin");
  out.beginObject(Layout::Block);
  out.key("format");
  out.string(device.plugin.format);
  out.key("identifier");
  out.string(device.plugin.identifier);
  out.key("name");
  out.string(device.plugin.name);
  out.key("vendor");
  out.string(device.plugin.vendor);
  out.key("version");
  out.string(device.plugin.version);
  out.endObject();
  out.key("state");
  if (device.stateRef.empty()) {
    out.null();
  } else {
    out.string(device.stateRef);
  }
  out.key("bypassed");
  out.boolean(device.bypassed);
  out.key("parameters");
  out.beginArray(Layout::Block);
  for (const model::ParameterValue &parameter : device.parameters) {
    out.beginObject(Layout::Inline);
    out.key("id");
    out.string(parameter.id);
    out.key("value");
    out.number(parameter.value);
    out.endObject();
  }
  out.endArray();
  out.endObject();
}

// The members of a target after its "type".
void writeTargetFields(JsonWriter &out, const model::MixerTarget &target) {
  out.string(type_name::mixer);
  out.key("parameter");
  out.string(nameOf(mixerParameterNames, target.parameter));
}

void writeTargetFields(JsonWriter &out, const model::SendTarget &target) {
  out.string(type_name::send);
  out.key("target");
  out.unsignedInteger(target.target.value);
}

void writeTargetFields(JsonWriter &out,
                       const model::DeviceParameterTarget &target) {
  out.string(type_name::deviceParameter);
  out.key("device");
  out.unsignedInteger(target.device.value);
  out.key("parameter");
  out.string(target.parameter);
}

void writeTarget(JsonWriter &out, const model::AutomationTarget &target) {
  out.beginObject(Layout::Inline);
  out.key("type");
  std::visit([&out](const auto &held) { writeTargetFields(out, held); },
             target);
  out.endObject();
}

void writeEnvelope(JsonWriter &out, const model::Envelope &envelope) {
  out.beginObject(Layout::Block);
  out.key("id");
  out.unsignedInteger(envelope.id.value);
  out.key("target");
  writeTarget(out, envelope.target);
  out.key("points");
  out.beginArray(Layout::Block);
  for (const model::AutomationPoint &point : envelope.points) {
    out.beginObject(Layout::Inline);
    out.key("time");
    out.signedInteger(point.time.ticks);
    out.key("value");
    out.number(point.value);
    out.endObject();
  }
  out.endArray();
  out.endObject();
}

void writeSlot(JsonWriter &out, const model::Slot &slot) {
  out.beginObject(Layout::Block);
  out.key("id");
  out.unsignedInteger(slot.id.value);
  out.key("scene");
  out.unsignedInteger(slot.scene.value);
  out.key("clip");
  if (slot.clip.has_value()) {
    writeClip(out, *slot.clip);
  } else {
    out.null();
  }
  out.endObject();
}

template <class Item>
void writeList(JsonWriter &out, std::string_view key,
               const std::vector<Item> &items,
               void (*writeItem)(JsonWriter &, const Item &)) {
  out.key(key);
  out.beginArray(Layout::Block);
  for (const Item &item : items) {
    writeItem(out, item);
  }
  out.endArray();
}

void writeTrack(JsonWriter &out, const model::Track &track) {
  out.beginObject(Layout::Block);
  out.key("id");
  out.unsignedInteger(track.id.value);
  out.key("kind");
  out.string(nameOf(trackKindNames, track.kind));
  out.key("name");
  out.string(track.name);
  out.key("colour");
  writeColour(out, track.colour);
  out.key("mixer");
  writeMixer(out, track.mixer);
  writeList(out, "devices", track.devices, writeDevice);
  writeList(out, "slots", track.slots, writeSlot);
  writeList(out, "arrangement", track.arrangement, writeClip);
  writeList(out, "automation", track.automation, writeEnvelope);
  out.endObject();
}

void forEachDevice(const model::Project &project,
                   const std::function<void(const model::Device &)> &visit) {
  const auto visitTrack = [&visit](const model::Track &track) {
    std::ranges::for_each(track.devices, visit);
  };
  std::ranges::for_each(project.tracks, visitTrack);
  std::ranges::for_each(project.returns, visitTrack);
  visitTrack(project.master);
}

} // namespace

std::string describeObject(ObjectRef object) {
  constexpr std::array<std::string_view, 9> kinds{
      "project", "media reference", "scene", "track", "slot",
      "device",  "envelope",        "clip",  "note",
  };
  return std::string{kinds.at(static_cast<std::size_t>(object.kind))} + " " +
         std::to_string(object.id);
}

Error tooLargeError(std::size_t size, std::size_t maxBytes) {
  return {
      .code = ErrorCode::TooLarge,
      .where = {},
      .message = "the document is " + std::to_string(size) +
                 " bytes long; the largest that is read or written is " +
                 std::to_string(maxBytes),
  };
}

Error ruleError(const std::vector<model::Failure> &failures) {
  // How many broken rules a message lists before it says "and more".
  constexpr std::size_t maxListed = 8;
  std::string message = "the document breaks the rules of the model: ";
  std::size_t listed = 0;
  for (const model::Failure &failure : failures) {
    if (listed == maxListed) {
      message += ", and " + std::to_string(failures.size() - listed) + " more";
      break;
    }
    if (listed > 0) {
      message += ", ";
    }
    message += std::string{model::faultName(failure.fault)} + " at " +
               describeObject(failure.object);
    ++listed;
  }
  return {.code = ErrorCode::Rule, .where = {}, .message = std::move(message)};
}

std::optional<Error> findUnwritable(const model::Project &project,
                                    std::string_view writtenBy) {
  if (!isValidUtf8(writtenBy) || writtenBy.contains('\0')) {
    return Error{
        .code = ErrorCode::Value,
        .where = "/written_by",
        .message = "the name of the writing application is not "
                   "UTF-8 text",
    };
  }
  if (const auto failures = model::validate(project); !failures.empty()) {
    return ruleError(failures);
  }
  const auto unshaped = std::ranges::find_if(
      project.media, [](const model::MediaReference &media) {
        return !isAllowedMediaPath(media.path);
      });
  if (unshaped != project.media.end()) {
    return Error{
        .code = ErrorCode::Value,
        .where = describeObject(unshaped->id),
        .message = "a media path is either relative, inside the project "
                   "folder and without empty, \".\" or \"..\" segments, "
                   "or absolute",
    };
  }
  return std::nullopt;
}

std::string encodeProject(const model::Project &project,
                          std::string_view writtenBy) {
  JsonWriter out;
  out.beginObject(Layout::Block);
  out.key("format");
  out.signedInteger(formatVersion);
  out.key("written_by");
  out.string(writtenBy);
  out.key("next_id");
  out.unsignedInteger(project.nextId);
  out.key("tempo");
  out.hundredths(static_cast<std::uint64_t>(project.tempo.hundredths()));
  out.key("time_signature");
  out.beginArray(Layout::Inline);
  out.signedInteger(project.timeSignature.numerator());
  out.signedInteger(project.timeSignature.denominator());
  out.endArray();
  out.key("loop");
  writeLoop(out, project.loop.enabled, project.loop.start, project.loop.length);
  out.key("start_marker");
  out.signedInteger(project.startMarker.ticks);
  out.key("launch_quantisation");
  writeLaunchQuantisation(out, project.launchQuantisation);
  writeList(out, "media", project.media, writeMedia);
  writeList(out, "scenes", project.scenes, writeScene);
  writeList(out, "tracks", project.tracks, writeTrack);
  writeList(out, "returns", project.returns, writeTrack);
  out.key("master");
  writeTrack(out, project.master);
  out.endObject();
  return out.finish();
}

std::vector<std::string> stateRefsOf(const model::Project &project) {
  std::vector<std::string> refs;
  forEachDevice(project, [&refs](const model::Device &device) {
    if (!device.stateRef.empty()) {
      refs.push_back(device.stateRef);
    }
  });
  std::ranges::sort(refs);
  const auto repeated = std::ranges::unique(refs);
  refs.erase(repeated.begin(), repeated.end());
  return refs;
}

std::vector<std::string> projectMediaPathsOf(const model::Project &project) {
  std::vector<std::string> paths;
  for (const model::MediaReference &media : project.media) {
    if (isProjectMediaPath(media.path)) {
      paths.push_back(media.path);
    }
  }
  std::ranges::sort(paths);
  const auto repeated = std::ranges::unique(paths);
  paths.erase(repeated.begin(), repeated.end());
  return paths;
}

} // namespace lina::format::detail

namespace lina::format {

std::expected<DocumentImage, Error>
writeDocument(const model::Document &document, std::string_view writtenBy) {
  return detail::guarded([&] -> std::expected<DocumentImage, Error> {
    const model::Project &project = document.project();
    if (auto unwritable = detail::findUnwritable(project, writtenBy)) {
      return std::unexpected(std::move(*unwritable));
    }
    std::string bytes = detail::encodeProject(project, writtenBy);
    if (bytes.size() > maxDocumentBytes) {
      return std::unexpected(
          detail::tooLargeError(bytes.size(), maxDocumentBytes));
    }
    return DocumentImage{std::move(bytes), detail::stateRefsOf(project),
                         detail::projectMediaPathsOf(project)};
  });
}

} // namespace lina::format
