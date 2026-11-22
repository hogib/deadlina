// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/testing/fake_playback.hpp"

#include <stdexcept>

namespace lina::ui::testing {

void FakePlayback::setProject(const model::Project &newProject,
                              const std::filesystem::path &folder) {
  if (projectFailures > 0) {
    --projectFailures;
    throw std::runtime_error("the fake engine refused the project");
  }
  ++projectsSet;
  mediaFolder = folder;
  project = newProject;
  changeSets.clear();
}

void FakePlayback::applyChanges(const model::Project &newProject,
                                const model::ChangeSet &changes) {
  if (failures > 0) {
    --failures;
    throw std::runtime_error("the fake engine failed on purpose");
  }
  project = newProject;
  changeSets.push_back(changes);
}

void FakePlayback::play(const model::Project & /*project*/,
                        model::TickPosition from) {
  if (!output) {
    throw std::logic_error("FakePlayback::play without an output");
  }
  playing = true;
  position = from;
  playedFrom.push_back(from);
}

void FakePlayback::stop() {
  playing = false;
  ++stops;
}

bridge::PlaybackState
FakePlayback::state(const model::Project & /*project*/) const {
  return {
      .playing = playing,
      .position = playing ? position : model::TickPosition{.ticks = 0},
  };
}

} // namespace lina::ui::testing
