// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/bridge/engine_playback.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace lina::bridge {

namespace {

// 128-bit integers are a compiler extension that both supported compilers
// have; the keyword silences the pedantic warning about it. A tick count
// (up to 2^53) times a rate does not fit 64 bits.
__extension__ using Wide = __int128;

/// floor(numerator / denominator + 1/2) for a numerator that is not
/// negative and a positive denominator: ticks, frames and elapsed time are
/// never negative here.
Wide roundedQuotient(Wide numerator, Wide denominator) {
  return ((2 * numerator) + denominator) / (2 * denominator);
}

constexpr std::int64_t secondsPerMinute = 60;
/// Tempo is stored in hundredths of a beat per minute.
constexpr std::int64_t tempoScale = 100;

std::int64_t wholeSampleRate(double rate) {
  if (!(rate > 0.0) || std::floor(rate) != rate || rate > 1.0e7) {
    throw std::invalid_argument(
        "EnginePlayback needs a whole-number sample rate");
  }
  return static_cast<std::int64_t>(rate);
}

} // namespace

std::int64_t frameOfTick(model::TickPosition position, model::Tempo tempo,
                         std::int64_t sampleRate) {
  // frames = ticks / ticksPerQuarter * 60 / bpm * rate
  const Wide numerator =
      Wide{position.ticks} * secondsPerMinute * tempoScale * Wide{sampleRate};
  const Wide denominator = Wide{tempo.hundredths()} * model::ticksPerQuarter;
  return static_cast<std::int64_t>(roundedQuotient(numerator, denominator));
}

model::TickPosition tickOfFrame(std::int64_t frame, model::Tempo tempo,
                                std::int64_t sampleRate) {
  const Wide numerator =
      Wide{frame} * tempo.hundredths() * model::ticksPerQuarter;
  const Wide denominator = Wide{sampleRate} * secondsPerMinute * tempoScale;
  return {static_cast<std::int64_t>(roundedQuotient(numerator, denominator))};
}

EnginePlayback::EnginePlayback(engine::Engine &engine, QObject *parent)
    : QObject(parent), engine_(&engine) {
  timer_.setInterval(paceInterval);
  connect(&timer_, &QTimer::timeout, this, [this] { pace(); });
}

EnginePlayback::~EnginePlayback() { closeOutput(); }

void EnginePlayback::openSilentOutput(const engine::StreamFormat &format,
                                      Clock clock) {
  const std::int64_t rate = wholeSampleRate(format.sampleRate);
  if (!clock) {
    throw std::invalid_argument("EnginePlayback needs a clock");
  }
  stop();
  engine_->openManualDevice(format);
  sampleRate_ = rate;
  clock_ = std::move(clock);
}

void EnginePlayback::closeOutput() {
  if (!engine_->hasManualDevice()) {
    return;
  }
  stop();
  engine_->closeManualDevice();
  clock_ = nullptr;
  sampleRate_ = 0;
}

void EnginePlayback::advance(std::int64_t frames) {
  if (frames <= 0 || !engine_->isPlaying()) {
    return;
  }
  // The audio goes nowhere: see the header.
  static_cast<void>(engine_->process(frames));
  frame_ += frames;
}

void EnginePlayback::setProject(const model::Project &project,
                                const std::filesystem::path &mediaFolder) {
  engine_->setProject(project, mediaFolder);
}

void EnginePlayback::applyChanges(const model::Project &project,
                                  const model::ChangeSet &changes) {
  engine_->applyChanges(project, changes);
}

bool EnginePlayback::hasOutput() const { return engine_->hasManualDevice(); }

void EnginePlayback::play(const model::Project &project,
                          model::TickPosition from) {
  if (!hasOutput()) {
    throw std::logic_error("EnginePlayback::play needs an output");
  }
  frame_ = frameOfTick(from, project.tempo, sampleRate_);
  engine_->play(frame_);
  paceBase_ = clock_();
  paced_ = 0;
  timer_.start();
}

void EnginePlayback::stop() {
  timer_.stop();
  if (engine_->isPlaying()) {
    engine_->stop();
  }
}

PlaybackState EnginePlayback::state(const model::Project &project) const {
  if (!engine_->isPlaying()) {
    return {};
  }
  return {
      .playing = true,
      .position = tickOfFrame(frame_, project.tempo, sampleRate_),
  };
}

void EnginePlayback::pace() {
  const std::chrono::nanoseconds now = clock_();
  const std::int64_t elapsed = std::max<std::int64_t>(
      0, std::chrono::duration_cast<std::chrono::nanoseconds>(now - paceBase_)
             .count());
  const auto due = static_cast<std::int64_t>(
      roundedQuotient(Wide{elapsed} * sampleRate_, Wide{1'000'000'000}));
  const std::int64_t behind = due - paced_;
  const std::int64_t limit = sampleRate_ / 4;
  const std::int64_t frames = std::min(behind, limit);
  advance(frames);
  paced_ += frames;
  if (behind > limit) {
    paceBase_ = now;
    paced_ = 0;
  }
}

} // namespace lina::bridge
