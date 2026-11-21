// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on device chains.
#include "apply_parts.hpp"

#include <optional>
#include <variant>

namespace lina::model::detail {

namespace {

// True for an envelope that automates a parameter of `device`.
bool automates(const Envelope &envelope, DeviceId device) {
  const auto *target = std::get_if<DeviceParameterTarget>(&envelope.target);
  return target != nullptr && target->device == device;
}

// The chain with `device` put in at `index`, if the chain rules allow that.
// A failure names the track: the device may not have an identity yet.
std::expected<std::vector<Device>, Failure>
chainWith(const Track &track, const Device &device, std::size_t index) {
  std::vector<Device> chain = track.devices;
  chain.insert(iteratorAt(chain, index), device);
  Failures failures;
  checkDevices(track.kind, chain, ObjectRef{track.id}, failures);
  if (!failures.empty()) {
    return fail(failures.front().fault, ObjectRef{track.id});
  }
  return chain;
}

Result moveWithinChain(Track &track, DeviceId device, std::size_t index) {
  const ObjectRef self{device};
  if (index >= track.devices.size()) {
    return fail(Fault::IndexOutOfRange, self);
  }
  const std::size_t from = indexOf(
      track.devices, std::ranges::find(track.devices, device, &Device::id));
  if (from == index) {
    return unchanged();
  }
  std::vector<Device> chain = track.devices;
  moveElement(chain, from, index);
  Failures failures;
  checkDevices(track.kind, chain, ObjectRef{track.id}, failures);
  if (!failures.empty()) {
    return std::unexpected(failures.front());
  }
  track.devices = std::move(chain);
  Applied applied{
      .inverse = Command{MoveDevice{
          .device = device,
          .track = track.id,
          .index = from,
      }},
      .changes = {},
  };
  applied.changes.changed(ObjectRef{track.id}, projectRef,
                          Property::DeviceOrder);
  return applied;
}

// Moves a device to another track, with the automation of its parameters.
Result moveAcrossTracks(Track &source, Track &destination, DeviceId device,
                        std::size_t index) {
  const ObjectRef self{device};
  if (index > destination.devices.size()) {
    return fail(Fault::IndexOutOfRange, self);
  }
  const auto at = std::ranges::find(source.devices, device, &Device::id);
  auto chain = chainWith(destination, *at, index);
  if (!chain.has_value()) {
    return std::unexpected(chain.error());
  }

  const ObjectRef from{source.id};
  const ObjectRef to{destination.id};
  Applied applied{
      .inverse = Command{MoveDevice{
          .device = device,
          .track = source.id,
          .index = indexOf(source.devices, at),
      }},
      .changes = {},
  };
  destination.devices = std::move(*chain);
  source.devices.erase(at);
  applied.changes.moved(self, from, to, Property::Container);
  // Both chains are told, so that a device that leaves and comes back at
  // another place inside one transaction still shows on its track.
  applied.changes.changed(from, projectRef, Property::DeviceOrder);
  applied.changes.changed(to, projectRef, Property::DeviceOrder);
  for (Envelope &envelope :
       takeEnvelopes(source, [device](const Envelope &candidate) {
         return automates(candidate, device);
       })) {
    applied.changes.moved(ObjectRef{envelope.id}, from, to,
                          Property::Container);
    insertEnvelope(destination, std::move(envelope));
  }
  return applied;
}

template <Property Which, class Type>
Result setDeviceField(const DeviceSite<Track> &site, Type &field,
                      const Value<Which, Type> &wanted) {
  if (field == wanted.value) {
    return unchanged();
  }
  Applied applied{
      .inverse = Command{SetDeviceProperty{
          .device = site.device->id,
          .property = Value<Which, Type>{field},
      }},
      .changes = {},
  };
  field = wanted.value;
  applied.changes.changed(ObjectRef{site.device->id}, ObjectRef{site.track->id},
                          Which);
  return applied;
}

Result setParameter(const DeviceSite<Track> &site, ParameterSetting wanted) {
  Device &device = *site.device;
  const ObjectRef self{device.id};
  if (wanted.value.has_value()) {
    wanted.value = canonical(*wanted.value);
  }
  const bool valueAllowed =
      !wanted.value.has_value() || inRange(*wanted.value, 0.0, 1.0);
  if (wanted.id.empty() || !valueAllowed) {
    return fail(Fault::ValueOutOfRange, self);
  }
  if (!isStorableText(wanted.id)) {
    return fail(Fault::TextInvalid, self);
  }
  const auto at = std::ranges::lower_bound(device.parameters, wanted.id, {},
                                           &ParameterValue::id);
  const bool stored = at != device.parameters.end() && at->id == wanted.id;
  const std::optional<double> previous =
      stored ? std::optional{at->value} : std::nullopt;
  if (previous == wanted.value) {
    return unchanged();
  }
  Applied applied{
      .inverse = Command{SetDeviceProperty{
          .device = device.id,
          .property =
              ParameterSettingValue{{.id = wanted.id, .value = previous}},
      }},
      .changes = {},
  };
  if (!wanted.value.has_value()) {
    device.parameters.erase(at);
  } else if (stored) {
    at->value = *wanted.value;
  } else {
    device.parameters.insert(at, {.id = wanted.id, .value = *wanted.value});
  }
  applied.changes.changed(self, ObjectRef{site.track->id},
                          Property::Parameters);
  return applied;
}

// One overload per device property.
Result setProperty(const DeviceSite<Track> &site, const BypassedValue &wanted) {
  return setDeviceField(site, site.device->bypassed, wanted);
}
Result setProperty(const DeviceSite<Track> &site, const StateValue &wanted) {
  if (!isStateRef(wanted.value)) {
    return fail(Fault::ValueOutOfRange, ObjectRef{site.device->id});
  }
  return setDeviceField(site, site.device->stateRef, wanted);
}
Result setProperty(const DeviceSite<Track> &site,
                   const ParameterSettingValue &wanted) {
  return setParameter(site, wanted.value);
}

} // namespace

Result applyOne(Project &project, const AddDevice &command) {
  const ObjectRef trackRef{command.track};
  Track *track = trackIn(project, command.track);
  if (track == nullptr) {
    return fail(Fault::UnknownObject, trackRef);
  }
  if (command.device.id.assigned()) {
    return fail(Fault::IdInvalid, trackRef);
  }
  if (command.index > track->devices.size()) {
    return fail(Fault::IndexOutOfRange, trackRef);
  }
  Device device = command.device;
  for (ParameterValue &parameter : device.parameters) {
    parameter.value = canonical(parameter.value);
  }
  // Checked without an identity, so that no failure can name one that was
  // never handed out.
  auto chain = chainWith(*track, device, command.index);
  if (!chain.has_value()) {
    return std::unexpected(chain.error());
  }
  if (!idsAvailable(project, 1)) {
    return fail(Fault::IdsExhausted, trackRef);
  }
  const DeviceId id{project.nextId++};
  iteratorAt(*chain, command.index)->id = id;
  track->devices = std::move(*chain);
  Applied applied{.inverse = Command{RemoveDevice{id}}, .changes = {}};
  applied.changes.added(ObjectRef{id}, trackRef);
  return applied;
}

Result applyOne(Project &project, const RemoveDevice &command) {
  const ObjectRef self{command.device};
  const auto site = deviceIn(project, command.device);
  if (site.device == nullptr) {
    return fail(Fault::UnknownObject, self);
  }
  Track &track = *site.track;
  const ObjectRef trackRef{track.id};
  const auto at = std::ranges::find(track.devices, command.device, &Device::id);
  Applied applied;
  RestoreDevice restore;
  restore.track = track.id;
  restore.index = indexOf(track.devices, at);
  restore.envelopes =
      takeEnvelopes(track, [&command](const Envelope &candidate) {
        return automates(candidate, command.device);
      });
  for (const Envelope &envelope : restore.envelopes) {
    applied.changes.removed(ObjectRef{envelope.id}, trackRef);
  }
  applied.changes.removed(self, trackRef);
  restore.device = std::move(*at);
  track.devices.erase(at);
  applied.inverse = std::move(restore);
  return applied;
}

Applied restoreOne(Project &project, const RestoreDevice &restore) {
  Track &track = *trackIn(project, restore.track);
  const ObjectRef trackRef{track.id};
  track.devices.insert(iteratorAt(track.devices, restore.index),
                       restore.device);
  Applied applied{
      .inverse = Command{RemoveDevice{restore.device.id}},
      .changes = {},
  };
  applied.changes.added(ObjectRef{restore.device.id}, trackRef);
  for (const Envelope &envelope : restore.envelopes) {
    insertEnvelope(track, envelope);
    applied.changes.added(ObjectRef{envelope.id}, trackRef);
  }
  return applied;
}

Result applyOne(Project &project, const MoveDevice &command) {
  const auto site = deviceIn(project, command.device);
  if (site.device == nullptr) {
    return fail(Fault::UnknownObject, ObjectRef{command.device});
  }
  Track *destination = trackIn(project, command.track);
  if (destination == nullptr) {
    return fail(Fault::UnknownObject, ObjectRef{command.track});
  }
  if (destination == site.track) {
    return moveWithinChain(*site.track, command.device, command.index);
  }
  return moveAcrossTracks(*site.track, *destination, command.device,
                          command.index);
}

Result applyOne(Project &project, const SetDeviceProperty &command) {
  const auto site = deviceIn(project, command.device);
  if (site.device == nullptr) {
    return fail(Fault::UnknownObject, ObjectRef{command.device});
  }
  return std::visit(
      [&site](const auto &wanted) { return setProperty(site, wanted); },
      command.property);
}

} // namespace lina::model::detail
