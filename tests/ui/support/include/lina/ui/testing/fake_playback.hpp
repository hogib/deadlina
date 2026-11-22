// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_TESTING_FAKE_PLAYBACK_HPP
#define LINA_UI_TESTING_FAKE_PLAYBACK_HPP

#include "lina/bridge/playback.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace lina::ui::testing {

/// A playback engine that remembers what it was told and plays whatever
/// position the test sets.
class FakePlayback final : public bridge::Playback {
public:
  void setProject(const model::Project &project,
                  const std::filesystem::path &mediaFolder) override;
  void applyChanges(const model::Project &project,
                    const model::ChangeSet &changes) override;
  [[nodiscard]] bool hasOutput() const override { return output; }
  void play(const model::Project &project, model::TickPosition from) override;
  void stop() override;
  [[nodiscard]] bridge::PlaybackState
  state(const model::Project &project) const override;

  // --- What the test sets ---

  bool output{true};
  /// Where the engine says it is while playing.
  model::TickPosition position;
  /// The next applyChanges throws this many times.
  int failures{0};
  /// The next setProject throws this many times.
  int projectFailures{0};

  // --- What the fake was told ---

  int projectsSet{0};
  std::filesystem::path mediaFolder;
  /// The project of the last setProject or applyChanges.
  std::optional<model::Project> project;
  /// Every change set since the last setProject, in order.
  std::vector<model::ChangeSet> changeSets;
  std::vector<model::TickPosition> playedFrom;
  bool playing{false};
  /// Every call of stop(), whether or not it was playing.
  int stops{0};
};

} // namespace lina::ui::testing

#endif // LINA_UI_TESTING_FAKE_PLAYBACK_HPP
