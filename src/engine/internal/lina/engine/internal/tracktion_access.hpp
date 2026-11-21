// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_ENGINE_INTERNAL_TRACKTION_ACCESS_HPP
#define LINA_ENGINE_INTERNAL_TRACKTION_ACCESS_HPP

#include "lina/engine/engine.hpp"
#include "lina/engine/internal/device_registry.hpp"
#include "lina/engine/internal/projection.hpp"

#include <tracktion_engine/tracktion_engine.h>

namespace lina::engine::internal {

/// The Tracktion objects behind an Engine.
///
/// For code inside the engine tier only: tests that build an edit by hand or
/// look at what the projection made, and code that registers plugins. This
/// header is not on lina_engine's public include path; a target that needs
/// it links lina::engine_internals.
struct TracktionAccess {
  static tracktion::Engine &engine(Engine &engine);
  static tracktion::Edit &edit(Engine &engine);
  /// Where the plugins that devices can stand for are registered. Register
  /// before the project that uses them is set.
  static DeviceRegistry &devices(Engine &engine);
  static const Projection &projection(Engine &engine);
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_TRACKTION_ACCESS_HPP
