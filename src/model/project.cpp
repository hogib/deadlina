// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/model/project.hpp"

#include "lookup.hpp"

#include <variant>

namespace lina::model {

std::string_view faultName(Fault fault) {
  switch (fault) {
  case Fault::UnknownObject:
    return "unknown-object";
  case Fault::IndexOutOfRange:
    return "index-out-of-range";
  case Fault::ValueOutOfRange:
    return "value-out-of-range";
  case Fault::WrongTrackKind:
    return "wrong-track-kind";
  case Fault::WrongClipKind:
    return "wrong-clip-kind";
  case Fault::SlotOccupied:
    return "slot-occupied";
  case Fault::ClipOverlap:
    return "clip-overlap";
  case Fault::NoteOverlap:
    return "note-overlap";
  case Fault::IdInvalid:
    return "id-invalid";
  case Fault::MediaInUse:
    return "media-in-use";
  case Fault::MediaMissing:
    return "media-missing";
  case Fault::DeviceChainOrder:
    return "device-chain-order";
  case Fault::SlotsMismatch:
    return "slots-mismatch";
  case Fault::SendsMismatch:
    return "sends-mismatch";
  case Fault::OrderViolation:
    return "order-violation";
  case Fault::EnvelopeTarget:
    return "envelope-target";
  case Fault::WrongInputKind:
    return "wrong-input-kind";
  case Fault::IdsExhausted:
    return "ids-exhausted";
  case Fault::TextInvalid:
    return "text-invalid";
  case Fault::NoTransaction:
    return "no-transaction";
  case Fault::TransactionOpen:
    return "transaction-open";
  case Fault::NothingToUndo:
    return "nothing-to-undo";
  case Fault::NothingToRedo:
    return "nothing-to-redo";
  }
  return "unknown-fault";
}

Project makeEmptyProject() {
  Project project;
  project.master.id = TrackId{project.nextId++};
  project.master.kind = TrackKind::Master;
  project.master.name = "Master";
  return project;
}

bool sameContent(const Project &a, const Project &b) {
  return a.tempo == b.tempo && a.timeSignature == b.timeSignature &&
         a.loop == b.loop && a.startMarker == b.startMarker &&
         a.launchQuantisation == b.launchQuantisation && a.media == b.media &&
         a.scenes == b.scenes && a.tracks == b.tracks &&
         a.returns == b.returns && a.master == b.master;
}

const Scene *findScene(const Project &project, SceneId id) {
  return detail::findById(project.scenes, id);
}

const MediaReference *findMedia(const Project &project, MediaId id) {
  return detail::findById(project.media, id);
}

const Track *findTrack(const Project &project, TrackId id) {
  return detail::trackIn(project, id);
}

SlotLocation findSlot(const Project &project, SlotId id) {
  const auto site = detail::slotIn(project, id);
  return {.track = site.track, .slot = site.slot};
}

ClipLocation findClip(const Project &project, ClipId id) {
  const auto site = detail::clipIn(project, id);
  return {.track = site.track, .slot = site.slot, .clip = site.clip};
}

DeviceLocation findDevice(const Project &project, DeviceId id) {
  const auto site = detail::deviceIn(project, id);
  return {.track = site.track, .device = site.device};
}

EnvelopeLocation findEnvelope(const Project &project, EnvelopeId id) {
  const auto site = detail::envelopeIn(project, id);
  return {.track = site.track, .envelope = site.envelope};
}

const Note *findNote(const Clip &clip, NoteId id) {
  const auto *midi = std::get_if<MidiContent>(&clip.content);
  return midi == nullptr ? nullptr : detail::findById(midi->notes, id);
}

} // namespace lina::model
