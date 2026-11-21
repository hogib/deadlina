// SPDX-License-Identifier: GPL-3.0-or-later
//
// A document and an engine that plays it: every edit goes through a command
// and reaches the engine as a change set, the way the application does it.
#ifndef LINA_TESTS_ENGINE_PROJECTION_FIXTURE_HPP
#define LINA_TESTS_ENGINE_PROJECTION_FIXTURE_HPP

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/testing/engine_fixture.hpp"
#include "lina/engine/testing/projection_support.hpp"
#include "lina/engine/testing/test_plugins.hpp"
#include "lina/model/document.hpp"
#include "support/builders.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

namespace lina::engine::testing {

struct ProjectionFixture : EngineFixture {
  explicit ProjectionFixture(std::string_view name) : EngineFixture(name) {
    engine.setProject(document.project(), scratch);
  }

  [[nodiscard]] const model::Project &project() const {
    return document.project();
  }

  /// Applies a command that must succeed, as one transaction, and hands the
  /// change set to the engine.
  model::ChangeSet edit(const model::Command &command) {
    const auto changes = document.perform("test", command);
    REQUIRE(changes.has_value());
    engine.applyChanges(document.project(), *changes);
    return *changes;
  }

  /// Applies several commands as one transaction and hands the engine the
  /// one change set that covers them.
  model::ChangeSet editAll(std::span<const model::Command> commands) {
    const auto changes = document.perform("test", commands);
    REQUIRE(changes.has_value());
    engine.applyChanges(document.project(), *changes);
    return *changes;
  }

  /// As edit(), for a command that creates one object of a kind; returns
  /// the identity the document gave it.
  template <model::ObjectKind Kind>
  model::Id<Kind> add(const model::Command &command) {
    const auto id = edit(command).template firstAdded<Kind>();
    REQUIRE(id.has_value());
    return id.value_or(model::Id<Kind>{});
  }

  model::TrackId addTrack(model::TrackKind kind) {
    const std::size_t index = kind == model::TrackKind::Return
                                  ? project().returns.size()
                                  : project().tracks.size();
    return add<model::ObjectKind::Track>(
        model::AddTrack{.kind = kind, .name = "track", .index = index});
  }

  model::DeviceId addDevice(model::TrackId track, model::Device device) {
    const model::Track *target = model::findTrack(project(), track);
    REQUIRE(target != nullptr);
    return add<model::ObjectKind::Device>(model::AddDevice{
        .track = track,
        .device = std::move(device),
        .index = target->devices.size(),
    });
  }

  /// A MIDI track with the test instrument.
  model::TrackId addToneTrack() {
    const model::TrackId track = addTrack(model::TrackKind::Midi);
    addDevice(track, toneDevice());
    return track;
  }

  model::ClipId addClip(model::TrackId track, model::Clip clip) {
    return add<model::ObjectKind::Clip>(
        model::AddClip{.container = track, .clip = std::move(clip)});
  }

  void set(model::TrackId track, model::TrackProperty property) {
    edit(model::SetTrackProperty{
        .track = track,
        .property = std::move(property),
    });
  }

  void set(model::ClipId clip, model::ClipProperty property) {
    edit(model::SetClipProperty{.clip = clip, .property = std::move(property)});
  }

  void setTempo(std::int32_t hundredths) {
    const auto tempo = model::Tempo::fromHundredths(hundredths);
    REQUIRE(tempo.has_value());
    edit(model::SetProjectProperty{
        model::TempoValue{tempo.value_or(model::Tempo{})},
    });
  }

  /// Plays the arrangement from the start on a fresh manual device.
  AudioBuffer playFromStart(double sampleRate, std::int64_t frameCount,
                            int blockSize = 512) {
    engine.openManualDevice({.sampleRate = sampleRate, .blockSize = blockSize});
    engine.play();
    return engine.process(frameCount);
  }

  model::Document document;
};

} // namespace lina::engine::testing

#endif // LINA_TESTS_ENGINE_PROJECTION_FIXTURE_HPP
