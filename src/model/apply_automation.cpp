// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on automation envelopes.
#include "apply_parts.hpp"

namespace lina::model::detail {

Result applyOne(Project &project, const AddEnvelope &command) {
  // The envelope has no identity yet, so failures name the track.
  const ObjectRef trackRef{command.track};
  Track *track = trackIn(project, command.track);
  if (track == nullptr) {
    return fail(Fault::UnknownObject, trackRef);
  }
  if (command.envelope.id.assigned()) {
    return fail(Fault::IdInvalid, trackRef);
  }
  Envelope envelope = command.envelope;
  envelope.points = canonical(std::move(envelope.points));
  Failures failures;
  checkEnvelope(project, *track, envelope, failures);
  if (!failures.empty()) {
    return fail(failures.front().fault, trackRef);
  }
  const bool targetTaken =
      std::ranges::any_of(track->automation, [&](const Envelope &other) {
        return other.target == envelope.target;
      });
  if (targetTaken) {
    return fail(Fault::EnvelopeTarget, trackRef);
  }
  if (!idsAvailable(project, 1)) {
    return fail(Fault::IdsExhausted, trackRef);
  }

  envelope.id = EnvelopeId{project.nextId++};
  Applied applied{
      .inverse = Command{RemoveEnvelope{envelope.id}},
      .changes = {},
  };
  applied.changes.added(ObjectRef{envelope.id}, trackRef);
  insertEnvelope(*track, std::move(envelope));
  return applied;
}

Result applyOne(Project &project, const RemoveEnvelope &command) {
  const ObjectRef self{command.envelope};
  const auto site = envelopeIn(project, command.envelope);
  if (site.envelope == nullptr) {
    return fail(Fault::UnknownObject, self);
  }
  Applied applied;
  applied.changes.removed(self, ObjectRef{site.track->id});
  RestoreEnvelope restore;
  restore.track = site.track->id;
  restore.envelope = std::move(*site.envelope);
  std::erase_if(site.track->automation, [&command](const Envelope &envelope) {
    return envelope.id == command.envelope;
  });
  applied.inverse = std::move(restore);
  return applied;
}

Applied restoreOne(Project &project, const RestoreEnvelope &restore) {
  insertEnvelope(*trackIn(project, restore.track), restore.envelope);
  Applied applied{
      .inverse = Command{RemoveEnvelope{restore.envelope.id}},
      .changes = {},
  };
  applied.changes.added(ObjectRef{restore.envelope.id},
                        ObjectRef{restore.track});
  return applied;
}

Result applyOne(Project &project, const SetEnvelopePoints &command) {
  const ObjectRef self{command.envelope};
  const auto site = envelopeIn(project, command.envelope);
  if (site.envelope == nullptr) {
    return fail(Fault::UnknownObject, self);
  }
  Envelope &envelope = *site.envelope;
  std::vector<AutomationPoint> points = canonical(command.points);
  if (envelope.points == points) {
    return unchanged();
  }
  std::swap(envelope.points, points);
  Failures failures;
  checkEnvelope(project, *site.track, envelope, failures);
  if (!failures.empty()) {
    envelope.points = std::move(points);
    return std::unexpected(failures.front());
  }
  Applied applied{
      .inverse = Command{SetEnvelopePoints{
          .envelope = command.envelope,
          .points = std::move(points),
      }},
      .changes = {},
  };
  applied.changes.changed(self, ObjectRef{site.track->id}, Property::Points);
  return applied;
}

} // namespace lina::model::detail
