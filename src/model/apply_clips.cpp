// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on clips and on the notes and controller data inside MIDI clips.
#include "apply_parts.hpp"

#include <optional>
#include <tuple>
#include <variant>

namespace lina::model::detail {

namespace {

// A place for a clip: a slot, or (slot null) the arrangement of `track`.
struct Destination {
  Track *track{nullptr};
  Slot *slot{nullptr};

  [[nodiscard]] ObjectRef parent() const {
    return slot != nullptr ? ObjectRef{slot->id} : ObjectRef{track->id};
  }
};

std::expected<Destination, Failure> resolve(Project &project,
                                            const ClipContainer &container) {
  if (const auto *slotId = std::get_if<SlotId>(&container)) {
    const auto site = slotIn(project, *slotId);
    if (site.slot == nullptr) {
      return fail(Fault::UnknownObject, ObjectRef{*slotId});
    }
    return Destination{.track = site.track, .slot = site.slot};
  }
  const TrackId trackId = std::get<TrackId>(container);
  Track *track = trackIn(project, trackId);
  if (track == nullptr) {
    return fail(Fault::UnknownObject, ObjectRef{trackId});
  }
  if (!isRegular(track->kind)) {
    return fail(Fault::WrongTrackKind, ObjectRef{trackId});
  }
  return Destination{.track = track, .slot = nullptr};
}

ClipContainer containerOf(const ClipSite<Track> &site) {
  if (site.slot != nullptr) {
    return site.slot->id;
  }
  return site.track->id;
}

ObjectRef parentOf(const ClipSite<Track> &site) {
  return site.slot != nullptr ? ObjectRef{site.slot->id}
                              : ObjectRef{site.track->id};
}

// What stops a clip with this extent from going to `destination`, if anything.
// `self` is the clip being placed, which may already be there; an overlap is
// reported on `blame`.
std::optional<Failure> placementFailure(const Destination &destination,
                                        const ClipExtent &extent, ClipId self,
                                        ObjectRef blame) {
  if (destination.slot != nullptr) {
    const std::optional<Clip> &occupant = destination.slot->clip;
    if (occupant.has_value() && occupant->id != self) {
      return Failure{
          .fault = Fault::SlotOccupied,
          .object = ObjectRef{destination.slot->id},
      };
    }
    return std::nullopt;
  }
  if (overlapsOther(destination.track->arrangement, extent, self)) {
    return Failure{.fault = Fault::ClipOverlap, .object = blame};
  }
  return std::nullopt;
}

void place(const Destination &destination, Clip clip) {
  if (destination.slot != nullptr) {
    destination.slot->clip = std::move(clip);
    return;
  }
  std::vector<Clip> &arrangement = destination.track->arrangement;
  const auto after = std::ranges::upper_bound(
      arrangement, clip.extent.position, {},
      [](const Clip &other) { return other.extent.position; });
  arrangement.insert(after, std::move(clip));
}

Clip takeOut(const ClipSite<Track> &site) {
  Clip clip = std::move(*site.clip);
  if (site.slot != nullptr) {
    site.slot->clip.reset();
  } else {
    std::erase_if(site.track->arrangement,
                  [id = clip.id](const Clip &other) { return other.id == id; });
  }
  return clip;
}

bool anyIdAssigned(const Clip &clip) {
  bool assigned = false;
  auto visit = [&assigned](ObjectRef object, ObjectRef /*parent*/) {
    assigned = assigned || object.id != 0;
  };
  walkClip(clip, projectRef, visit);
  return assigned;
}

// Sets a property of a clip, unless it already has that value, and takes it
// back if `check` finds a broken rule. `check` looks only at what the
// property can break, so that renaming a clip does not walk its notes.
template <Property Which, class Type, class Check>
Result setClipField(const ClipSite<Track> &site, Type &field,
                    const Value<Which, Type> &wanted, Check check) {
  Type value = canonical(wanted.value);
  if (field == value) {
    return unchanged();
  }
  std::swap(field, value);
  Failures failures;
  check(failures);
  if (!failures.empty()) {
    field = std::move(value);
    return std::unexpected(failures.front());
  }
  Applied applied{
      .inverse = Command{SetClipProperty{
          .clip = site.clip->id,
          .property = Value<Which, Type>{std::move(value)},
      }},
      .changes = {},
  };
  applied.changes.changed(ObjectRef{site.clip->id}, parentOf(site), Which);
  return applied;
}

// Sets a property of the clip itself.
template <Property Which, class Type>
Result setHeaderField(const ClipSite<Track> &site, Type &field,
                      const Value<Which, Type> &wanted) {
  return setClipField(site, field, wanted, [&site](Failures &failures) {
    checkClipHeader(*site.clip, site.track->kind, site.slot != nullptr,
                    failures);
  });
}

// Sets a property that only audio clips have.
template <Property Which, class Type>
Result setAudioField(const Project &project, const ClipSite<Track> &site,
                     Type AudioContent::*member,
                     const Value<Which, Type> &wanted) {
  const ObjectRef self{site.clip->id};
  auto *audio = std::get_if<AudioContent>(&site.clip->content);
  if (audio == nullptr) {
    return fail(Fault::WrongClipKind, self);
  }
  return setClipField(site, audio->*member, wanted,
                      [&project, audio, self](Failures &failures) {
                        checkAudio(project, *audio, self, failures);
                      });
}

// One overload per clip property: where it lives.
Result setProperty(const Project & /*project*/, const ClipSite<Track> &site,
                   const NameValue &wanted) {
  return setHeaderField(site, site.clip->name, wanted);
}
Result setProperty(const Project & /*project*/, const ClipSite<Track> &site,
                   const ColourValue &wanted) {
  return setHeaderField(site, site.clip->colour, wanted);
}
Result setProperty(const Project & /*project*/, const ClipSite<Track> &site,
                   const LoopValue &wanted) {
  return setHeaderField(site, site.clip->loop, wanted);
}
Result setProperty(const Project & /*project*/, const ClipSite<Track> &site,
                   const LaunchOverrideValue &wanted) {
  return setHeaderField(site, site.clip->launchQuantisation, wanted);
}
Result setProperty(const Project & /*project*/, const ClipSite<Track> &site,
                   const ExtentValue &wanted) {
  const bool inArrangement = site.slot == nullptr;
  Result result = setClipField(
      site, site.clip->extent, wanted, [&site, inArrangement](Failures &f) {
        const Clip &clip = *site.clip;
        checkClipHeader(clip, site.track->kind, !inArrangement, f);
        if (f.empty() && inArrangement &&
            overlapsOther(site.track->arrangement, clip.extent, clip.id)) {
          f.push_back(
              {.fault = Fault::ClipOverlap, .object = ObjectRef{clip.id}});
        }
      });
  if (result.has_value() && inArrangement) {
    // A new position can change the order; sorting a sorted list is harmless.
    sortArrangement(site.track->arrangement);
  }
  return result;
}
Result setProperty(const Project &project, const ClipSite<Track> &site,
                   const GainValue &wanted) {
  return setAudioField(project, site, &AudioContent::gainDb, wanted);
}
Result setProperty(const Project &project, const ClipSite<Track> &site,
                   const FollowTempoValue &wanted) {
  return setAudioField(project, site, &AudioContent::followTempo, wanted);
}
Result setProperty(const Project &project, const ClipSite<Track> &site,
                   const StretchModeValue &wanted) {
  return setAudioField(project, site, &AudioContent::stretchMode, wanted);
}
Result setProperty(const Project &project, const ClipSite<Track> &site,
                   const TransposeValue &wanted) {
  return setAudioField(project, site, &AudioContent::transposeCents, wanted);
}
Result setProperty(const Project &project, const ClipSite<Track> &site,
                   const WarpValue &wanted) {
  return setAudioField(project, site, &AudioContent::warp, wanted);
}
Result setProperty(const Project &project, const ClipSite<Track> &site,
                   const RegionValue &wanted) {
  return setAudioField(project, site, &AudioContent::region, wanted);
}

struct MidiSite {
  ObjectRef clip;
  ObjectRef parent;
  MidiContent *midi{nullptr};
};

std::expected<MidiSite, Failure> midiClip(Project &project, ClipId id) {
  const auto site = clipIn(project, id);
  if (site.clip == nullptr) {
    return fail(Fault::UnknownObject, ObjectRef{id});
  }
  auto *midi = std::get_if<MidiContent>(&site.clip->content);
  if (midi == nullptr) {
    return fail(Fault::WrongClipKind, ObjectRef{id});
  }
  return MidiSite{
      .clip = ObjectRef{id},
      .parent = parentOf(site),
      .midi = midi,
  };
}

// The clip's notes with `extra` (in model order) merged in: one pass, no sort.
std::vector<Note> mergedNotes(const MidiContent &midi,
                              const std::vector<Note> &extra) {
  std::vector<Note> merged;
  merged.reserve(midi.notes.size() + extra.size());
  std::ranges::merge(midi.notes, extra, std::back_inserter(merged), noteBefore);
  return merged;
}

std::vector<NoteId> idsOf(const std::vector<Note> &notes) {
  std::vector<NoteId> ids;
  ids.reserve(notes.size());
  for (const Note &note : notes) {
    ids.push_back(note.id);
  }
  return ids;
}

// The first identity that occurs twice, if any.
std::optional<NoteId> firstDuplicate(std::vector<NoteId> ids) {
  std::ranges::sort(ids);
  const auto twice = std::ranges::adjacent_find(ids);
  return twice == ids.end() ? std::nullopt : std::optional{*twice};
}

auto controllerKey(const ControllerEnvelope &envelope) {
  return std::tuple{envelope.kind, envelope.controller};
}

} // namespace

Result applyOne(Project &project, const AddClip &command) {
  const auto destination = resolve(project, command.container);
  if (!destination.has_value()) {
    return std::unexpected(destination.error());
  }
  // The clip has no identity yet, so failures name where it was meant to go.
  const ObjectRef parent = destination->parent();
  if (anyIdAssigned(command.clip)) {
    return fail(Fault::IdInvalid, parent);
  }
  Clip clip = command.clip;
  auto *midi = std::get_if<MidiContent>(&clip.content);
  if (midi != nullptr) {
    sortNotes(midi->notes);
  } else {
    auto &audio = std::get<AudioContent>(clip.content);
    audio.gainDb = canonical(audio.gainDb);
  }
  Failures failures;
  checkClip(project, clip, destination->track->kind,
            destination->slot != nullptr, failures);
  if (!failures.empty()) {
    return fail(failures.front().fault, parent);
  }
  if (const auto failure =
          placementFailure(*destination, clip.extent, clip.id, parent)) {
    return std::unexpected(*failure);
  }
  // The clip, and each of its notes.
  if (!idsAvailable(project, 1 + (midi != nullptr ? midi->notes.size() : 0))) {
    return fail(Fault::IdsExhausted, parent);
  }

  clip.id = ClipId{project.nextId++};
  if (midi != nullptr) {
    for (Note &note : midi->notes) {
      note.id = NoteId{project.nextId++};
    }
  }
  Applied applied{.inverse = Command{RemoveClip{clip.id}}, .changes = {}};
  addedClip(applied.changes, clip, parent);
  place(*destination, std::move(clip));
  return applied;
}

Result applyOne(Project &project, const RemoveClip &command) {
  const auto site = clipIn(project, command.clip);
  if (site.clip == nullptr) {
    return fail(Fault::UnknownObject, ObjectRef{command.clip});
  }
  Applied applied;
  removedClip(applied.changes, *site.clip, parentOf(site));
  const ClipContainer container = containerOf(site);
  applied.inverse = RestoreClip{.container = container, .clip = takeOut(site)};
  return applied;
}

Applied restoreOne(Project &project, const RestoreClip &restore) {
  Destination destination;
  if (const auto *slotId = std::get_if<SlotId>(&restore.container)) {
    const auto site = slotIn(project, *slotId);
    destination = {.track = site.track, .slot = site.slot};
  } else {
    destination.track =
        findById(project.tracks, std::get<TrackId>(restore.container));
  }
  Applied applied{
      .inverse = Command{RemoveClip{restore.clip.id}},
      .changes = {},
  };
  addedClip(applied.changes, restore.clip, destination.parent());
  place(destination, restore.clip);
  return applied;
}

Result applyOne(Project &project, const MoveClip &command) {
  const ObjectRef self{command.clip};
  const auto site = clipIn(project, command.clip);
  if (site.clip == nullptr) {
    return fail(Fault::UnknownObject, self);
  }
  const auto destination = resolve(project, command.container);
  if (!destination.has_value()) {
    return std::unexpected(destination.error());
  }
  if (destination->track->kind != site.track->kind) {
    return fail(Fault::WrongTrackKind, self);
  }
  const bool toSlot = destination->slot != nullptr;
  ClipExtent extent = site.clip->extent;
  const TickPosition previousPosition =
      std::exchange(extent.position, command.position);
  if (!isStorable(extent.position, extent.length) ||
      (toSlot && command.position != TickPosition{0})) {
    return fail(Fault::ValueOutOfRange, self);
  }
  if (const auto failure =
          placementFailure(*destination, extent, command.clip, self)) {
    return std::unexpected(*failure);
  }

  const ObjectRef previousParent = parentOf(site);
  const ObjectRef parent = destination->parent();
  PropertySet properties;
  if (parent != previousParent) {
    properties |= Property::Container;
  }
  if (command.position != previousPosition) {
    properties |= Property::Extent;
  }
  if (properties.empty()) {
    return unchanged();
  }
  Applied applied{
      .inverse = Command{MoveClip{
          .clip = command.clip,
          .container = containerOf(site),
          .position = previousPosition,
      }},
      .changes = {},
  };
  Clip clip = takeOut(site);
  clip.extent = extent;
  place(*destination, std::move(clip));
  applied.changes.moved(self, previousParent, parent, properties);
  return applied;
}

Result applyOne(Project &project, const SetClipProperty &command) {
  const auto site = clipIn(project, command.clip);
  if (site.clip == nullptr) {
    return fail(Fault::UnknownObject, ObjectRef{command.clip});
  }
  return std::visit(
      [&project, &site](const auto &wanted) {
        return setProperty(project, site, wanted);
      },
      command.property);
}

Result applyOne(Project &project, const AddNotes &command) {
  const auto site = midiClip(project, command.clip);
  if (!site.has_value()) {
    return std::unexpected(site.error());
  }
  // The identities are only handed out if the notes are accepted, so until
  // then a failure names the clip, never a note.
  std::vector<Note> fresh = command.notes;
  if (!idsAvailable(project, fresh.size())) {
    return fail(Fault::IdsExhausted, site->clip);
  }
  std::uint64_t nextId = project.nextId;
  for (Note &note : fresh) {
    if (note.id.assigned()) {
      return fail(Fault::IdInvalid, site->clip);
    }
    note.id = NoteId{nextId++};
  }
  if (fresh.empty()) {
    return unchanged();
  }
  const std::vector<NoteId> ids = idsOf(fresh);
  sortNotes(fresh);
  std::vector<Note> merged = mergedNotes(*site->midi, fresh);
  Failures failures;
  checkNotes(merged, failures);
  if (!failures.empty()) {
    return fail(failures.front().fault, site->clip);
  }

  project.nextId = nextId;
  site->midi->notes = std::move(merged);
  Applied applied{
      .inverse = Command{RemoveNotes{.clip = command.clip, .notes = ids}},
      .changes = {},
  };
  for (const NoteId id : ids) {
    applied.changes.added(ObjectRef{id}, site->clip);
  }
  return applied;
}

Result applyOne(Project &project, const RemoveNotes &command) {
  const auto site = midiClip(project, command.clip);
  if (!site.has_value()) {
    return std::unexpected(site.error());
  }
  if (firstDuplicate(command.notes).has_value()) {
    return fail(Fault::IdInvalid, site->clip);
  }
  std::vector<NoteId> wanted = command.notes;
  std::ranges::sort(wanted);

  std::vector<Note> &notes = site->midi->notes;
  const auto goes = [&wanted](const Note &note) {
    return std::ranges::binary_search(wanted, note.id);
  };
  Applied applied;
  RestoreNotes restore{.clip = command.clip, .notes = {}};
  for (const Note &note : notes) {
    if (goes(note)) {
      restore.notes.push_back(note);
      applied.changes.removed(ObjectRef{note.id}, site->clip);
    }
  }
  if (restore.notes.size() != wanted.size()) {
    // Some identity is not a note of this clip. It may not exist at all, so
    // the failure names the clip.
    return fail(Fault::UnknownObject, site->clip);
  }
  std::erase_if(notes, goes);
  applied.inverse = std::move(restore);
  return applied;
}

Applied restoreOne(Project &project, const RestoreNotes &restore) {
  const auto site = clipIn(project, restore.clip);
  auto &midi = std::get<MidiContent>(site.clip->content);
  midi.notes = mergedNotes(midi, restore.notes);
  Applied applied{
      .inverse = Command{RemoveNotes{
          .clip = restore.clip,
          .notes = idsOf(restore.notes),
      }},
      .changes = {},
  };
  for (const Note &note : restore.notes) {
    applied.changes.added(ObjectRef{note.id}, ObjectRef{restore.clip});
  }
  return applied;
}

Result applyOne(Project &project, const SetNotes &command) {
  const auto site = midiClip(project, command.clip);
  if (!site.has_value()) {
    return std::unexpected(site.error());
  }
  if (firstDuplicate(idsOf(command.notes)).has_value()) {
    return fail(Fault::IdInvalid, site->clip);
  }

  // Sorted by identity for the lookups, then back into model order.
  std::vector<Note> updated = site->midi->notes;
  std::ranges::sort(updated, {}, &Note::id);
  Applied applied;
  SetNotes restore{.clip = command.clip, .notes = {}};
  for (const Note &wanted : command.notes) {
    const auto at = std::ranges::lower_bound(updated, wanted.id, {}, &Note::id);
    if (at == updated.end() || at->id != wanted.id) {
      // Not a note of this clip, and possibly of no clip: name the clip.
      return fail(Fault::UnknownObject, site->clip);
    }
    if (*at != wanted) {
      restore.notes.push_back(std::exchange(*at, wanted));
      applied.changes.changed(ObjectRef{wanted.id}, site->clip,
                              Property::NoteData);
    }
  }
  if (applied.changes.empty()) {
    return unchanged();
  }
  sortNotes(updated);
  Failures failures;
  checkNotes(updated, failures);
  if (!failures.empty()) {
    return std::unexpected(failures.front());
  }

  site->midi->notes = std::move(updated);
  applied.inverse = Command{std::move(restore)};
  return applied;
}

Result applyOne(Project &project, const SetControllerData &command) {
  const auto site = midiClip(project, command.clip);
  if (!site.has_value()) {
    return std::unexpected(site.error());
  }
  std::vector<ControllerEnvelope> controllers = site->midi->controllers;
  const auto at = std::ranges::lower_bound(
      controllers, controllerKey(command.data), {}, controllerKey);
  const bool stored = at != controllers.end() &&
                      controllerKey(*at) == controllerKey(command.data);
  ControllerEnvelope previous{
      .kind = command.data.kind,
      .controller = command.data.controller,
      .points = {},
  };
  if (stored) {
    previous.points = at->points;
  }
  if (previous.points == command.data.points) {
    return unchanged();
  }
  if (command.data.points.empty()) {
    controllers.erase(at);
  } else if (stored) {
    at->points = command.data.points;
  } else {
    controllers.insert(at, command.data);
  }
  Failures failures;
  checkControllers(controllers, site->clip, failures);
  if (!failures.empty()) {
    return std::unexpected(failures.front());
  }

  site->midi->controllers = std::move(controllers);
  Applied applied{
      .inverse = Command{SetControllerData{
          .clip = command.clip,
          .data = std::move(previous),
      }},
      .changes = {},
  };
  applied.changes.changed(site->clip, site->parent, Property::Controllers);
  return applied;
}

} // namespace lina::model::detail
