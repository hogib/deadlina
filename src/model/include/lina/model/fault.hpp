// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the model reports a broken rule: as a value naming the rule and the
// object, never as an exception. Commands fail with one `Failure`; validation
// returns all it finds.
#ifndef LINA_MODEL_FAULT_HPP
#define LINA_MODEL_FAULT_HPP

#include "lina/model/ids.hpp"

#include <cstdint>
#include <string_view>

namespace lina::model {

enum class Fault : std::uint8_t {
  // A command names an object that is not in the document.
  UnknownObject,
  // An insertion or move index is past the end of its list.
  IndexOutOfRange,
  // A number, position or length is outside what the model allows, or a
  // required text is empty.
  ValueOutOfRange,
  // The track's kind does not allow this: clips on a return, an audio clip on a
  // MIDI track, pan on the master, and so on.
  WrongTrackKind,
  // The clip's content does not allow this: notes in an audio clip, gain on a
  // MIDI clip.
  WrongClipKind,
  // The slot already holds a clip.
  SlotOccupied,
  // Two arrangement clips on one track would overlap.
  ClipOverlap,
  // Two notes of the same pitch in one clip would overlap.
  NoteOverlap,
  // An identity is unassigned where one is required, assigned where the
  // document must assign it, not yet handed out by the document, or already in
  // use.
  IdInvalid,
  // The media reference is still used by an audio clip.
  MediaInUse,
  // An audio clip points at a media reference the project does not have.
  MediaMissing,
  // An instrument anywhere but first in a MIDI track's chain.
  DeviceChainOrder,
  // A track's slots do not match the project's scenes one to one, in order.
  SlotsMismatch,
  // A track's sends do not match the project's return tracks one to one, in
  // order.
  SendsMismatch,
  // A list that the model keeps sorted is not sorted, or holds a duplicate key.
  OrderViolation,
  // An envelope's target is not on its track, or two envelopes share a target.
  EnvelopeTarget,
  // The track's kind does not take this input: a MIDI device on an audio track,
  // any input on a return track or the master.
  WrongInputKind,
  // The identity counter has reached `maxNextId`: the document cannot hand
  // out the identities a command needs, or holds a counter past that limit.
  IdsExhausted,
  // A text is not well-formed UTF-8, or holds the character U+0000.
  TextInvalid,
  // A command was applied with no transaction open.
  NoTransaction,
  // Begin, undo or redo was asked for while a transaction is open.
  TransactionOpen,
  NothingToUndo,
  NothingToRedo,
};

// A stable, lower-case name for a fault, for messages and logs.
std::string_view faultName(Fault fault);

// One broken rule and the object it was found on.
struct Failure {
  Fault fault{Fault::UnknownObject};
  ObjectRef object;

  friend bool operator==(const Failure &, const Failure &) = default;
};

} // namespace lina::model

#endif // LINA_MODEL_FAULT_HPP
