// SPDX-License-Identifier: GPL-3.0-or-later
//
// Identities (docs/design/13-architecture.md): 64-bit numbers taken from one
// counter stored in the document, never reused and never random. An identity is
// typed by the kind of object it names, so a clip's cannot be passed where a
// track's is expected.
#ifndef LINA_MODEL_IDS_HPP
#define LINA_MODEL_IDS_HPP

#include <compare>
#include <cstdint>

namespace lina::model {

// The kinds of object that have an identity. The order is containment order: an
// object's kind always comes after its container's, which is what change sets
// sort by.
enum class ObjectKind : std::uint8_t {
  Project,
  Media,
  Scene,
  Track,
  Slot,
  Device,
  Envelope,
  Clip,
  Note,
};

// The largest value the identity counter of a document may have: 2^62. A
// document that created a million objects a second would need more than a
// hundred thousand years to get there, and the limit leaves the counter far
// from where a 64-bit number wraps round.
inline constexpr std::uint64_t maxNextId = std::uint64_t{1} << 62U;

// The identity of one object of kind `Kind`. Zero means "not assigned".
template <ObjectKind Kind> struct Id {
  std::uint64_t value{0};

  [[nodiscard]] constexpr bool assigned() const { return value != 0; }

  friend constexpr auto operator<=>(Id, Id) = default;
};

using MediaId = Id<ObjectKind::Media>;
using SceneId = Id<ObjectKind::Scene>;
using TrackId = Id<ObjectKind::Track>;
using SlotId = Id<ObjectKind::Slot>;
using DeviceId = Id<ObjectKind::Device>;
using EnvelopeId = Id<ObjectKind::Envelope>;
using ClipId = Id<ObjectKind::Clip>;
using NoteId = Id<ObjectKind::Note>;

// An identity with its kind erased into a value, for change sets and error
// reports.
struct ObjectRef {
  ObjectKind kind{ObjectKind::Project};
  std::uint64_t id{0};

  constexpr ObjectRef() = default;
  constexpr ObjectRef(ObjectKind kindOfObject, std::uint64_t idOfObject)
      : kind(kindOfObject), id(idOfObject) {}
  // Implicit on purpose: a typed identity is usable wherever an erased one is
  // wanted.
  template <ObjectKind Kind>
  // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,misc-explicit-constructor)
  constexpr ObjectRef(Id<Kind> typed) : kind(Kind), id(typed.value) {}

  friend constexpr auto operator<=>(const ObjectRef &,
                                    const ObjectRef &) = default;
};

// The project itself, which has no identity of its own.
inline constexpr ObjectRef projectRef{ObjectKind::Project, 0};

} // namespace lina::model

#endif // LINA_MODEL_IDS_HPP
