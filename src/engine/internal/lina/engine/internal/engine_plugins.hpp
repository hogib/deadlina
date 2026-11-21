// SPDX-License-Identifier: GPL-3.0-or-later
//
// The two engine plugins the projection puts into the edit itself: the mixer
// strip's gain stage and the placeholder for a device whose plugin is not
// known. Neither is a device of the document.
#ifndef LINA_ENGINE_INTERNAL_ENGINE_PLUGINS_HPP
#define LINA_ENGINE_INTERNAL_ENGINE_PLUGINS_HPP

#include "lina/engine/internal/mixing.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace lina::engine::internal {

/// One gain per side, set from the message thread and applied on the audio
/// thread.
///
/// The engine's own volume plugin is not used for LINA's strips: its fader
/// curve turns a decibel value into a gain through a single-precision
/// position and back, it tops out at +6 dB before any pan law, and none of
/// its pan laws is constant power with unity at the centre. This plugin
/// multiplies by exactly the gains it is given (mixing.hpp).
///
/// A change of gain is not a step: the gain moves to its new value in a
/// straight line over `rampSeconds`, counted in sample frames, so the result
/// does not depend on the block size. When the playback graph is first made
/// the gain starts at its target.
class StripGainPlugin final : public tracktion::Plugin {
public:
  using Ptr = juce::ReferenceCountedObjectPtr<StripGainPlugin>;

  static constexpr const char *xmlTypeName = "linaStripGain";
  static constexpr double rampSeconds = 0.005;

  explicit StripGainPlugin(const tracktion::PluginCreationInfo &info);
  ~StripGainPlugin() override;

  StripGainPlugin(const StripGainPlugin &) = delete;
  StripGainPlugin &operator=(const StripGainPlugin &) = delete;
  StripGainPlugin(StripGainPlugin &&) = delete;
  StripGainPlugin &operator=(StripGainPlugin &&) = delete;

  static const char *getPluginName() { return "LINA strip gain"; }
  static juce::ValueTree create();

  /// The gains to move to. Message thread.
  void setGains(StereoGain gains);
  [[nodiscard]] StereoGain gains() const;

  juce::String getName() const override { return getPluginName(); }
  juce::String getPluginType() override { return xmlTypeName; }
  juce::String getSelectableDescription() override { return getName(); }

  void initialise(const tracktion::PluginInitialisationInfo &info) override;
  void deinitialise() override;
  void reset() override;
  void applyToBuffer(const tracktion::PluginRenderContext &context)
      [[clang::nonblocking]] override;

private:
  static constexpr std::size_t sideCount = 2;

  void jumpToTarget() [[clang::nonblocking]];

  std::array<std::atomic<float>, sideCount> wanted_{1.0F, 1.0F};
  /// Audio thread only: where the ramp is and where it is going.
  std::array<float, sideCount> current_{1.0F, 1.0F};
  std::array<float, sideCount> target_{1.0F, 1.0F};
  std::int64_t framesLeft_ = 0;
  std::int64_t rampFrames_ = 0;
};

/// Stands in for a device whose plugin the engine cannot make: audio and
/// notes pass through unchanged, so an unknown effect is transparent and an
/// unknown instrument is silent
/// (docs/design/09-mixer-routing-and-devices.md, "Devices", rule 9).
class PlaceholderPlugin final : public tracktion::Plugin {
public:
  static constexpr const char *xmlTypeName = "linaPlaceholder";

  explicit PlaceholderPlugin(const tracktion::PluginCreationInfo &info);
  ~PlaceholderPlugin() override;

  PlaceholderPlugin(const PlaceholderPlugin &) = delete;
  PlaceholderPlugin &operator=(const PlaceholderPlugin &) = delete;
  PlaceholderPlugin(PlaceholderPlugin &&) = delete;
  PlaceholderPlugin &operator=(PlaceholderPlugin &&) = delete;

  static const char *getPluginName() { return "LINA placeholder"; }
  static juce::ValueTree create();

  juce::String getName() const override { return getPluginName(); }
  juce::String getPluginType() override { return xmlTypeName; }
  juce::String getSelectableDescription() override { return getName(); }

  void initialise(const tracktion::PluginInitialisationInfo &info) override;
  void deinitialise() override;
  void applyToBuffer(const tracktion::PluginRenderContext &context)
      [[clang::nonblocking]] override;
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_ENGINE_PLUGINS_HPP
