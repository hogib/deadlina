// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_BRIDGE_ENGINE_PLAYBACK_HPP
#define LINA_BRIDGE_ENGINE_PLAYBACK_HPP

#include "lina/bridge/playback.hpp"
#include "lina/engine/engine.hpp"

#include <QObject>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <functional>

namespace lina::bridge {

/// The sample frame nearest to a tick under one tempo and a whole-number
/// sample rate; a tick exactly between two frames goes to the later one.
/// The same rule as the engine's own conversion.
[[nodiscard]] std::int64_t frameOfTick(model::TickPosition position,
                                       model::Tempo tempo,
                                       std::int64_t sampleRate);

/// The tick nearest to a frame, the later one on a tie. For display and for
/// "continue": never stored in the document.
[[nodiscard]] model::TickPosition
tickOfFrame(std::int64_t frame, model::Tempo tempo, std::int64_t sampleRate);

/// The interface's view of an engine::Engine.
///
/// The engine opens no sound device of the machine (engine.hpp); the only
/// output it has is its manual device, which produces audio only when asked
/// for frames. openSilentOutput() opens that device and asks it, from Qt's
/// event loop, for as many frames as real time says have passed while the
/// transport plays. The audio is thrown away: the transport runs and the
/// playhead moves, and nothing is heard. It stands in for a sound device
/// until the engine can open one, and uses no processor time while stopped.
///
/// Tests drive the output with advance() and a clock of their own, and read
/// no clock.
///
/// The playhead counts frames played since play() and is converted to ticks
/// with the tempo of the project passed to state(). After a tempo change
/// while playing it shows what the engine does: the engine keeps its place
/// in seconds.
///
/// Created, used and destroyed on the message thread, like the Engine,
/// which must outlive it.
class EnginePlayback final : public QObject, public Playback {
  Q_OBJECT

public:
  /// Time from an arbitrary fixed point, never going backwards.
  using Clock = std::function<std::chrono::nanoseconds()>;

  /// How often the silent output is driven while playing.
  static constexpr std::chrono::milliseconds paceInterval{10};

  explicit EnginePlayback(engine::Engine &engine, QObject *parent = nullptr);
  ~EnginePlayback() override;

  EnginePlayback(const EnginePlayback &) = delete;
  EnginePlayback &operator=(const EnginePlayback &) = delete;
  EnginePlayback(EnginePlayback &&) = delete;
  EnginePlayback &operator=(EnginePlayback &&) = delete;

  /// Opens the silent output described above, with a whole-number sample
  /// rate (std::invalid_argument otherwise). Replaces one already open and
  /// stops playback.
  void openSilentOutput(const engine::StreamFormat &format, Clock clock);

  /// Stops playback and closes the output. Does nothing without one.
  void closeOutput();

  /// Plays `frames` more frames now and throws the audio away. Does nothing
  /// while stopped.
  void advance(std::int64_t frames);

  void setProject(const model::Project &project,
                  const std::filesystem::path &mediaFolder) override;
  void applyChanges(const model::Project &project,
                    const model::ChangeSet &changes) override;
  [[nodiscard]] bool hasOutput() const override;
  void play(const model::Project &project, model::TickPosition from) override;
  void stop() override;
  [[nodiscard]] PlaybackState
  state(const model::Project &project) const override;

private:
  /// Catches up with real time, by at most a quarter of a second at once;
  /// beyond that the time is skipped, as a sound device would drop out.
  void pace();

  engine::Engine *engine_;
  Clock clock_;
  QTimer timer_;
  std::int64_t sampleRate_{0};
  /// The frame the next process() call starts at.
  std::int64_t frame_{0};
  /// Real time from which pacing counts, and the frames it has run since.
  /// Frames run by advance() from outside do not count.
  std::chrono::nanoseconds paceBase_{0};
  std::int64_t paced_{0};
};

} // namespace lina::bridge

#endif // LINA_BRIDGE_ENGINE_PLAYBACK_HPP
