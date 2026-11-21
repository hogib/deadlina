// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on the project itself, media references, scenes and tracks.
#include "apply_parts.hpp"

#include <ranges>
#include <variant>

namespace lina::model::detail {

namespace {

// Sets one of the project's own properties after `check` has passed the new
// value.
template <Property Which, class Type, class Check>
Result setProjectField(Type &field, const Value<Which, Type> &wanted,
                       Check check) {
  Failures failures;
  check(wanted.value, failures);
  if (!failures.empty()) {
    return std::unexpected(failures.front());
  }
  if (field == wanted.value) {
    return unchanged();
  }
  Applied applied{
      .inverse = Command{SetProjectProperty{Value<Which, Type>{field}}},
      .changes = {},
  };
  field = wanted.value;
  applied.changes.changed(projectRef, projectRef, Which);
  return applied;
}

// For properties whose type cannot hold a value the model forbids.
template <class Type>
void alwaysValid(const Type & /*value*/, Failures & /*failures*/) {}

// One overload per project property: where it lives and what its value must
// satisfy.
Result setProperty(Project &project, const TempoValue &wanted) {
  return setProjectField(project.tempo, wanted, alwaysValid<Tempo>);
}
Result setProperty(Project &project, const TimeSignatureValue &wanted) {
  return setProjectField(project.timeSignature, wanted,
                         alwaysValid<TimeSignature>);
}
Result setProperty(Project &project, const LoopRangeValue &wanted) {
  return setProjectField(project.loop, wanted, checkLoopRange);
}
Result setProperty(Project &project, const StartMarkerValue &wanted) {
  return setProjectField(project.startMarker, wanted, checkStartMarker);
}
Result setProperty(Project &project, const DefaultLaunchValue &wanted) {
  return setProjectField(project.launchQuantisation, wanted,
                         checkLaunchQuantisation);
}

bool usesMedia(const Clip &clip, MediaId media) {
  const auto *audio = std::get_if<AudioContent>(&clip.content);
  return audio != nullptr && audio->media == media;
}

bool usesMedia(const Project &project, MediaId media) {
  return std::ranges::any_of(project.tracks, [media](const Track &track) {
    return std::ranges::any_of(
               track.arrangement,
               [media](const Clip &clip) { return usesMedia(clip, media); }) ||
           std::ranges::any_of(track.slots, [media](const Slot &slot) {
             return slot.clip.has_value() && usesMedia(*slot.clip, media);
           });
  });
}

// Sets a property of a track's header, unless it already has that value, and
// takes it back if a rule objects.
template <Property Which, class Type>
Result setTrackField(const Project &project, Track &track, Type &field,
                     const Value<Which, Type> &wanted, bool applicable) {
  const ObjectRef self{track.id};
  if (!applicable) {
    return fail(Fault::WrongTrackKind, self);
  }
  Type value = canonical(wanted.value);
  if (field == value) {
    return unchanged();
  }
  std::swap(field, value);
  Failures failures;
  checkTrackHeader(project, track, failures);
  if (!failures.empty()) {
    field = std::move(value);
    return std::unexpected(failures.front());
  }
  Applied applied{
      .inverse = Command{SetTrackProperty{
          .track = track.id,
          .property = Value<Which, Type>{std::move(value)},
      }},
      .changes = {},
  };
  applied.changes.changed(self, projectRef, Which);
  return applied;
}

Result setSendLevel(Track &track, const Send &wanted) {
  const ObjectRef self{track.id};
  if (!isRegular(track.kind)) {
    return fail(Fault::WrongTrackKind, self);
  }
  const auto send =
      std::ranges::find(track.mixer.sends, wanted.target, &Send::target);
  if (send == track.mixer.sends.end()) {
    return fail(Fault::UnknownObject, ObjectRef{wanted.target});
  }
  const Send value = canonical(wanted);
  if (!inRange(value.levelDb, silenceDb, maxSendDb)) {
    return fail(Fault::ValueOutOfRange, self);
  }
  if (*send == value) {
    return unchanged();
  }
  Applied applied{
      .inverse = Command{SetTrackProperty{
          .track = track.id,
          .property = SendValue{*send},
      }},
      .changes = {},
  };
  *send = value;
  applied.changes.changed(self, projectRef, Property::Sends);
  return applied;
}

// One overload per track property: where it lives and which kinds of track
// have it.
Result setProperty(const Project &project, Track &track,
                   const NameValue &wanted) {
  return setTrackField(project, track, track.name, wanted, true);
}
Result setProperty(const Project &project, Track &track,
                   const ColourValue &wanted) {
  return setTrackField(project, track, track.colour, wanted, true);
}
Result setProperty(const Project &project, Track &track,
                   const VolumeValue &wanted) {
  return setTrackField(project, track, track.mixer.volumeDb, wanted, true);
}
Result setProperty(const Project &project, Track &track,
                   const PanValue &wanted) {
  return setTrackField(project, track, track.mixer.pan, wanted,
                       track.kind != TrackKind::Master);
}
Result setProperty(const Project &project, Track &track,
                   const MuteValue &wanted) {
  return setTrackField(project, track, track.mixer.mute, wanted,
                       track.kind != TrackKind::Master);
}
Result setProperty(const Project &project, Track &track,
                   const SoloValue &wanted) {
  return setTrackField(project, track, track.mixer.solo, wanted,
                       track.kind != TrackKind::Master);
}
Result setProperty(const Project &project, Track &track,
                   const ArmedValue &wanted) {
  return setTrackField(project, track, track.mixer.armed, wanted,
                       isRegular(track.kind));
}
Result setProperty(const Project &project, Track &track,
                   const MonitoringValue &wanted) {
  return setTrackField(project, track, track.mixer.monitoring, wanted,
                       isRegular(track.kind));
}
Result setProperty(const Project &project, Track &track,
                   const InputValue &wanted) {
  return setTrackField(project, track, track.mixer.input, wanted,
                       isRegular(track.kind));
}
Result setProperty(const Project & /*project*/, Track &track,
                   const SendValue &wanted) {
  return setSendLevel(track, wanted.value);
}

Slot emptySlot(Project &project, SceneId scene) {
  return {.id = SlotId{project.nextId++}, .scene = scene, .clip = std::nullopt};
}

Result removeRegularTrack(Project &project, std::vector<Track>::iterator at) {
  Applied applied;
  removedTrack(applied.changes, *at);
  const std::size_t index = indexOf(project.tracks, at);
  applied.inverse = RestoreTrack{
      .track = std::move(*at),
      .index = index,
      .sendLevels = {},
      .sendEnvelopes = {},
  };
  project.tracks.erase(at);
  return applied;
}

// A return track takes with it every send to it and the automation of those
// sends.
Result removeReturnTrack(Project &project, std::vector<Track>::iterator at) {
  const std::size_t index = indexOf(project.returns, at);
  const AutomationTarget target{SendTarget{at->id}};
  Applied applied;
  RestoreTrack restore;
  restore.index = index;
  for (Track &sender : project.tracks) {
    const ObjectRef senderRef{sender.id};
    const auto send = iteratorAt(sender.mixer.sends, index);
    restore.sendLevels.push_back(send->levelDb);
    sender.mixer.sends.erase(send);
    applied.changes.changed(senderRef, projectRef, Property::Sends);
    for (Envelope &envelope :
         takeEnvelopes(sender, [&target](const Envelope &candidate) {
           return candidate.target == target;
         })) {
      applied.changes.removed(ObjectRef{envelope.id}, senderRef);
      restore.sendEnvelopes.push_back(
          {.track = sender.id, .envelope = std::move(envelope)});
    }
  }
  removedTrack(applied.changes, *at);
  restore.track = std::move(*at);
  project.returns.erase(at);
  applied.inverse = std::move(restore);
  return applied;
}

} // namespace

Result applyOne(Project &project, const SetProjectProperty &command) {
  return std::visit(
      [&project](const auto &wanted) { return setProperty(project, wanted); },
      command.property);
}

Result applyOne(Project &project, const AddMedia &command) {
  if (command.media.id.assigned()) {
    return fail(Fault::IdInvalid, projectRef);
  }
  Failures failures;
  checkMedia(command.media, failures);
  if (!failures.empty()) {
    return fail(failures.front().fault, projectRef);
  }
  if (!idsAvailable(project, 1)) {
    return fail(Fault::IdsExhausted, projectRef);
  }
  MediaReference &media = project.media.emplace_back(command.media);
  media.id = MediaId{project.nextId++};
  Applied applied{.inverse = Command{RemoveMedia{media.id}}, .changes = {}};
  applied.changes.added(ObjectRef{media.id}, projectRef);
  return applied;
}

Result applyOne(Project &project, const RemoveMedia &command) {
  const ObjectRef self{command.media};
  const auto at =
      std::ranges::find(project.media, command.media, &MediaReference::id);
  if (at == project.media.end()) {
    return fail(Fault::UnknownObject, self);
  }
  if (usesMedia(project, command.media)) {
    return fail(Fault::MediaInUse, self);
  }
  Applied applied;
  applied.changes.removed(self, projectRef);
  const std::size_t index = indexOf(project.media, at);
  applied.inverse = RestoreMedia{.media = std::move(*at), .index = index};
  project.media.erase(at);
  return applied;
}

Applied restoreOne(Project &project, const RestoreMedia &restore) {
  project.media.insert(iteratorAt(project.media, restore.index), restore.media);
  Applied applied{
      .inverse = Command{RemoveMedia{restore.media.id}},
      .changes = {},
  };
  applied.changes.added(ObjectRef{restore.media.id}, projectRef);
  return applied;
}

Result applyOne(Project &project, const RelinkMedia &command) {
  const ObjectRef self{command.media};
  MediaReference *media = findById(project.media, command.media);
  if (media == nullptr) {
    return fail(Fault::UnknownObject, self);
  }
  if (command.path.empty()) {
    return fail(Fault::ValueOutOfRange, self);
  }
  if (!isStorableText(command.path)) {
    return fail(Fault::TextInvalid, self);
  }
  if (media->path == command.path) {
    return unchanged();
  }
  Applied applied{
      .inverse =
          Command{RelinkMedia{.media = command.media, .path = media->path}},
      .changes = {},
  };
  media->path = command.path;
  applied.changes.changed(self, projectRef, Property::Path);
  return applied;
}

Result applyOne(Project &project, const AddScene &command) {
  if (command.index > project.scenes.size()) {
    return fail(Fault::IndexOutOfRange, projectRef);
  }
  if (!isStorableText(command.name)) {
    return fail(Fault::TextInvalid, projectRef);
  }
  // The scene, and a slot for it on every track.
  if (!idsAvailable(project, 1 + project.tracks.size())) {
    return fail(Fault::IdsExhausted, projectRef);
  }
  const SceneId scene{project.nextId++};
  project.scenes.insert(iteratorAt(project.scenes, command.index),
                        Scene{.id = scene, .name = command.name});
  Applied applied{.inverse = Command{RemoveScene{scene}}, .changes = {}};
  applied.changes.added(ObjectRef{scene}, projectRef);
  for (Track &track : project.tracks) {
    const auto slot = track.slots.insert(iteratorAt(track.slots, command.index),
                                         emptySlot(project, scene));
    applied.changes.added(ObjectRef{slot->id}, ObjectRef{track.id});
  }
  return applied;
}

Result applyOne(Project &project, const RemoveScene &command) {
  const auto at = std::ranges::find(project.scenes, command.scene, &Scene::id);
  if (at == project.scenes.end()) {
    return fail(Fault::UnknownObject, ObjectRef{command.scene});
  }
  const std::size_t index = indexOf(project.scenes, at);
  Applied applied;
  RestoreScene restore{.scene = std::move(*at), .index = index, .slots = {}};
  for (Track &track : project.tracks) {
    const auto slot = iteratorAt(track.slots, index);
    removedSlot(applied.changes, *slot, ObjectRef{track.id});
    restore.slots.push_back(std::move(*slot));
    track.slots.erase(slot);
  }
  applied.changes.removed(ObjectRef{command.scene}, projectRef);
  project.scenes.erase(at);
  applied.inverse = std::move(restore);
  return applied;
}

Applied restoreOne(Project &project, const RestoreScene &restore) {
  project.scenes.insert(iteratorAt(project.scenes, restore.index),
                        restore.scene);
  Applied applied{
      .inverse = Command{RemoveScene{restore.scene.id}},
      .changes = {},
  };
  applied.changes.added(ObjectRef{restore.scene.id}, projectRef);
  for (const auto &[track, slot] :
       std::views::zip(project.tracks, restore.slots)) {
    track.slots.insert(iteratorAt(track.slots, restore.index), slot);
    addedSlot(applied.changes, slot, ObjectRef{track.id});
  }
  return applied;
}

Result applyOne(Project &project, const MoveScene &command) {
  const auto at = std::ranges::find(project.scenes, command.scene, &Scene::id);
  if (at == project.scenes.end()) {
    return fail(Fault::UnknownObject, ObjectRef{command.scene});
  }
  if (command.index >= project.scenes.size()) {
    return fail(Fault::IndexOutOfRange, ObjectRef{command.scene});
  }
  const std::size_t from = indexOf(project.scenes, at);
  if (from == command.index) {
    return unchanged();
  }
  Applied applied{
      .inverse = Command{MoveScene{.scene = command.scene, .index = from}},
      .changes = {},
  };
  moveElement(project.scenes, from, command.index);
  for (Track &track : project.tracks) {
    moveElement(track.slots, from, command.index);
  }
  applied.changes.changed(projectRef, projectRef, Property::SceneOrder);
  return applied;
}

Result applyOne(Project &project, const RenameScene &command) {
  const ObjectRef self{command.scene};
  Scene *scene = findById(project.scenes, command.scene);
  if (scene == nullptr) {
    return fail(Fault::UnknownObject, self);
  }
  if (scene->name == command.name) {
    return unchanged();
  }
  if (!isStorableText(command.name)) {
    return fail(Fault::TextInvalid, self);
  }
  Applied applied{
      .inverse =
          Command{RenameScene{.scene = command.scene, .name = scene->name}},
      .changes = {},
  };
  scene->name = command.name;
  applied.changes.changed(self, projectRef, Property::Name);
  return applied;
}

Result applyOne(Project &project, const AddTrack &command) {
  if (!isKnown(command.kind)) {
    return fail(Fault::ValueOutOfRange, projectRef);
  }
  if (command.kind == TrackKind::Master) {
    return fail(Fault::WrongTrackKind, projectRef);
  }
  const bool regular = isRegular(command.kind);
  std::vector<Track> &list = regular ? project.tracks : project.returns;
  if (command.index > list.size()) {
    return fail(Fault::IndexOutOfRange, projectRef);
  }
  if (!isStorableText(command.name)) {
    return fail(Fault::TextInvalid, projectRef);
  }
  // The track, and on an audio or MIDI track a slot for every scene.
  if (!idsAvailable(project, 1 + (regular ? project.scenes.size() : 0))) {
    return fail(Fault::IdsExhausted, projectRef);
  }

  Track track;
  track.id = TrackId{project.nextId++};
  track.kind = command.kind;
  track.name = command.name;
  Applied applied{.inverse = Command{RemoveTrack{track.id}}, .changes = {}};
  if (regular) {
    if (command.kind == TrackKind::Midi) {
      track.mixer.input = AllMidiInputs{};
    }
    for (const Scene &scene : project.scenes) {
      track.slots.push_back(emptySlot(project, scene.id));
    }
    for (const Track &returnTrack : project.returns) {
      track.mixer.sends.push_back(
          {.target = returnTrack.id, .levelDb = silenceDb});
    }
  } else {
    for (Track &sender : project.tracks) {
      sender.mixer.sends.insert(iteratorAt(sender.mixer.sends, command.index),
                                Send{.target = track.id, .levelDb = silenceDb});
      applied.changes.changed(ObjectRef{sender.id}, projectRef,
                              Property::Sends);
    }
  }
  addedTrack(applied.changes, track);
  list.insert(iteratorAt(list, command.index), std::move(track));
  return applied;
}

Result applyOne(Project &project, const RemoveTrack &command) {
  if (command.track == project.master.id) {
    return fail(Fault::WrongTrackKind, ObjectRef{command.track});
  }
  const auto regular =
      std::ranges::find(project.tracks, command.track, &Track::id);
  if (regular != project.tracks.end()) {
    return removeRegularTrack(project, regular);
  }
  const auto returnTrack =
      std::ranges::find(project.returns, command.track, &Track::id);
  if (returnTrack != project.returns.end()) {
    return removeReturnTrack(project, returnTrack);
  }
  return fail(Fault::UnknownObject, ObjectRef{command.track});
}

Applied restoreOne(Project &project, const RestoreTrack &restore) {
  const Track &track = restore.track;
  std::vector<Track> &list =
      isRegular(track.kind) ? project.tracks : project.returns;
  Applied applied{.inverse = Command{RemoveTrack{track.id}}, .changes = {}};
  // Empty for an audio or MIDI track, so these two loops do nothing then.
  for (const auto &[sender, level] :
       std::views::zip(project.tracks, restore.sendLevels)) {
    sender.mixer.sends.insert(iteratorAt(sender.mixer.sends, restore.index),
                              Send{.target = track.id, .levelDb = level});
    applied.changes.changed(ObjectRef{sender.id}, projectRef, Property::Sends);
  }
  for (const OwnedEnvelope &owned : restore.sendEnvelopes) {
    insertEnvelope(*findById(project.tracks, owned.track), owned.envelope);
    applied.changes.added(ObjectRef{owned.envelope.id}, ObjectRef{owned.track});
  }
  addedTrack(applied.changes, track);
  list.insert(iteratorAt(list, restore.index), track);
  return applied;
}

Result applyOne(Project &project, const MoveTrack &command) {
  const ObjectRef self{command.track};
  if (command.track == project.master.id) {
    return fail(Fault::WrongTrackKind, self);
  }
  const auto regular =
      std::ranges::find(project.tracks, command.track, &Track::id);
  const bool isReturn = regular == project.tracks.end();
  std::vector<Track> &list = isReturn ? project.returns : project.tracks;
  const auto at =
      isReturn ? std::ranges::find(list, command.track, &Track::id) : regular;
  if (at == list.end()) {
    return fail(Fault::UnknownObject, self);
  }
  if (command.index >= list.size()) {
    return fail(Fault::IndexOutOfRange, self);
  }
  const std::size_t from = indexOf(list, at);
  if (from == command.index) {
    return unchanged();
  }
  Applied applied{
      .inverse = Command{MoveTrack{.track = command.track, .index = from}},
      .changes = {},
  };
  moveElement(list, from, command.index);
  if (isReturn) {
    for (Track &sender : project.tracks) {
      moveElement(sender.mixer.sends, from, command.index);
    }
  }
  applied.changes.changed(projectRef, projectRef,
                          isReturn ? Property::ReturnOrder
                                   : Property::TrackOrder);
  return applied;
}

Result applyOne(Project &project, const SetTrackProperty &command) {
  Track *track = trackIn(project, command.track);
  if (track == nullptr) {
    return fail(Fault::UnknownObject, ObjectRef{command.track});
  }
  return std::visit(
      [&project, track](const auto &wanted) {
        return setProperty(project, *track, wanted);
      },
      command.property);
}

} // namespace lina::model::detail
