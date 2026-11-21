// SPDX-License-Identifier: GPL-3.0-or-later
//
// The seam between a device of the document and the engine plugin that
// stands for it. A device names its plugin with a descriptor (format and
// identifier); a binding registered for that descriptor says how to make the
// engine plugin and how to hand it the device's parameters. A device nobody
// has a binding for becomes a placeholder that passes audio through
// (docs/design/09-mixer-routing-and-devices.md, "Devices").
//
// Today only LINA's own test plugins register here. Hosting of real plugin
// formats plugs in at the same place.
#ifndef LINA_ENGINE_INTERNAL_DEVICE_REGISTRY_HPP
#define LINA_ENGINE_INTERNAL_DEVICE_REGISTRY_HPP

#include "lina/model/track.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

namespace lina::engine::internal {

/// How one plugin is made for a device and kept in step with it.
class DeviceBinding {
public:
  DeviceBinding() = default;
  virtual ~DeviceBinding() = default;

  DeviceBinding(const DeviceBinding &) = delete;
  DeviceBinding &operator=(const DeviceBinding &) = delete;
  DeviceBinding(DeviceBinding &&) = delete;
  DeviceBinding &operator=(DeviceBinding &&) = delete;

  /// The state from which the engine creates the plugin: at least its type.
  [[nodiscard]] virtual juce::ValueTree
  createState(const model::Device &device) const = 0;

  /// Hands the plugin what the device stores for it: parameter values and
  /// the reference to its state. Called after creation and whenever either
  /// changes. A parameter the device does not list is at the plugin's
  /// default; a parameter the plugin does not know is ignored.
  virtual void apply(tracktion::Plugin &plugin,
                     const model::Device &device) const = 0;
};

class DeviceRegistry {
public:
  /// Registers the binding for one plugin; a later one for the same format
  /// and identifier replaces the earlier.
  void add(std::string format, std::string identifier,
           std::unique_ptr<DeviceBinding> binding) {
    bindings_.insert_or_assign(
        std::pair{std::move(format), std::move(identifier)},
        std::move(binding));
  }

  /// The binding for a descriptor, or null when the plugin is not known.
  [[nodiscard]] const DeviceBinding *
  find(const model::PluginDescriptor &plugin) const {
    const auto found =
        bindings_.find(std::pair{plugin.format, plugin.identifier});
    return found == bindings_.end() ? nullptr : found->second.get();
  }

private:
  std::map<std::pair<std::string, std::string>, std::unique_ptr<DeviceBinding>>
      bindings_;
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_DEVICE_REGISTRY_HPP
