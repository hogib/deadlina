// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_BRIDGE_PLAYBACK_HPP
#define LINA_BRIDGE_PLAYBACK_HPP

#include "lina/model/change_set.hpp"
#include "lina/model/project.hpp"
#include "lina/model/time.hpp"

#include <filesystem>

namespace lina::bridge {

/// What the interface reads of the transport once per frame
/// (docs/design/13-architecture.md, "What flows back, and how").
struct PlaybackState {
  bool playing{false};
  /// Where playback is now, in the document's musical time. Meaningful
  /// while playing; the interface keeps the playhead itself while stopped.
  model::TickPosition position;

  friend bool operator==(const PlaybackState &,
                         const PlaybackState &) = default;
};

/// What the interface needs of the playback engine. The application gives
/// the interface an EnginePlayback; interface tests give it a fake.
///
/// Everything is called on the main thread. The document is never handed
/// over: each call gets the project as it is at that moment, and nothing is
/// read back from the engine into it.
class Playback {
public:
  Playback() = default;
  Playback(const Playback &) = delete;
  Playback &operator=(const Playback &) = delete;
  Playback(Playback &&) = delete;
  Playback &operator=(Playback &&) = delete;
  virtual ~Playback() = default;

  /// Plays `project` from now on, built anew. Relative media paths are
  /// looked for in `mediaFolder`, an absolute path.
  virtual void setProject(const model::Project &project,
                          const std::filesystem::path &mediaFolder) = 0;

  /// Follows one edit: `changes` is what the document returned, `project`
  /// the document after it. Every change set is passed on, in order.
  virtual void applyChanges(const model::Project &project,
                            const model::ChangeSet &changes) = 0;

  /// True when there is an output to play to.
  [[nodiscard]] virtual bool hasOutput() const = 0;

  /// Starts playback at `from`. Needs an output (std::logic_error
  /// otherwise).
  virtual void play(const model::Project &project,
                    model::TickPosition from) = 0;

  /// Stops playback. Does nothing when stopped.
  virtual void stop() = 0;

  [[nodiscard]] virtual PlaybackState
  state(const model::Project &project) const = 0;
};

} // namespace lina::bridge

#endif // LINA_BRIDGE_PLAYBACK_HPP
