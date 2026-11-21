// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/engine_plugins.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace lina::engine::internal {

namespace te = tracktion;

StripGainPlugin::StripGainPlugin(const te::PluginCreationInfo &info)
    : Plugin(info) {}

StripGainPlugin::~StripGainPlugin() { notifyListenersOfDeletion(); }

juce::ValueTree StripGainPlugin::create() {
  return te::createValueTree(te::IDs::PLUGIN, te::IDs::type, xmlTypeName);
}

void StripGainPlugin::setGains(StereoGain gains) {
  wanted_.at(0).store(gains.left, std::memory_order_relaxed);
  wanted_.at(1).store(gains.right, std::memory_order_relaxed);
}

StereoGain StripGainPlugin::gains() const {
  return {
      .left = wanted_.at(0).load(std::memory_order_relaxed),
      .right = wanted_.at(1).load(std::memory_order_relaxed),
  };
}

void StripGainPlugin::initialise(const te::PluginInitialisationInfo &info) {
  rampFrames_ = std::llround(info.sampleRate * rampSeconds);
  jumpToTarget();
}

void StripGainPlugin::deinitialise() { jumpToTarget(); }

void StripGainPlugin::reset() { jumpToTarget(); }

void StripGainPlugin::jumpToTarget() [[clang::nonblocking]] {
  for (std::size_t side = 0; side < sideCount; ++side) {
    // The indices are below the size of the arrays, so at() never throws.
    target_.at(side) = wanted_.at(side).load(std::memory_order_relaxed);
    current_.at(side) = target_.at(side);
  }
  framesLeft_ = 0;
}

void StripGainPlugin::applyToBuffer(const te::PluginRenderContext &context)
    [[clang::nonblocking]] {
  if (context.destBuffer == nullptr) {
    return;
  }
  for (std::size_t side = 0; side < sideCount; ++side) {
    const float wanted = wanted_.at(side).load(std::memory_order_relaxed);
    if (wanted != target_.at(side)) {
      target_.at(side) = wanted;
      framesLeft_ = rampFrames_;
    }
  }
  if (framesLeft_ == 0) {
    current_ = target_;
  }

  auto &buffer = *context.destBuffer;
  const auto frameCount = static_cast<std::size_t>(context.bufferNumSamples);
  const auto channelCount = static_cast<std::size_t>(buffer.getNumChannels());
  // Each side follows the same ramp, so it is walked once per channel from
  // the state at the start of the block.
  const auto startCurrent = current_;
  const auto startFramesLeft = framesLeft_;
  for (std::size_t channel = 0; channel < channelCount; ++channel) {
    const std::size_t side = channel % sideCount;
    const std::span samples(buffer.getWritePointer(static_cast<int>(channel),
                                                   context.bufferStartSample),
                            frameCount);
    float gain = startCurrent.at(side);
    std::int64_t left = startFramesLeft;
    for (float &sample : samples) {
      if (left > 0) {
        gain += (target_.at(side) - gain) / static_cast<float>(left);
        --left;
      } else {
        gain = target_.at(side);
      }
      sample *= gain;
    }
    current_.at(side) = gain;
  }
  framesLeft_ = std::max<std::int64_t>(
      0, startFramesLeft - static_cast<std::int64_t>(frameCount));
}

PlaceholderPlugin::PlaceholderPlugin(const te::PluginCreationInfo &info)
    : Plugin(info) {}

PlaceholderPlugin::~PlaceholderPlugin() { notifyListenersOfDeletion(); }

juce::ValueTree PlaceholderPlugin::create() {
  return te::createValueTree(te::IDs::PLUGIN, te::IDs::type, xmlTypeName);
}

void PlaceholderPlugin::initialise(
    [[maybe_unused]] const te::PluginInitialisationInfo &info) {}

void PlaceholderPlugin::deinitialise() {}

void PlaceholderPlugin::applyToBuffer(
    [[maybe_unused]] const te::PluginRenderContext &context)
    [[clang::nonblocking]] {}

} // namespace lina::engine::internal
