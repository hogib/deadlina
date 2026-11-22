// SPDX-License-Identifier: GPL-3.0-or-later
#include "harness.hpp"

#include <QQmlEngine>

#include <algorithm>
#include <stdexcept>

namespace lina::ui::testing {

Harness::Harness(QObject *parent) : QObject(parent) {}

Harness::~Harness() = default;

Shell *Harness::makeShell() {
  rigs_.push_back(std::make_unique<Rig>());
  Shell *shell = &rigs_.back()->shell;
  QQmlEngine::setObjectOwnership(shell, QQmlEngine::CppOwnership);
  return shell;
}

Harness::Rig &Harness::rigOf(const Shell *shell) {
  const auto found = std::ranges::find_if(
      rigs_, [shell](const auto &rig) { return &rig->shell == shell; });
  if (found == rigs_.end()) {
    throw std::invalid_argument("not a shell of this harness");
  }
  return **found;
}

void Harness::setOutput(Shell *shell, bool output) {
  Rig &rig = rigOf(shell);
  rig.playback.output = output;
  rig.shell.transport()->refreshOutput();
}

void Harness::setEnginePosition(Shell *shell, int quarters) {
  rigOf(shell).playback.position = {quarters * model::ticksPerQuarter};
}

bool Harness::enginePlaying(Shell *shell) {
  return rigOf(shell).playback.playing;
}

void Harness::setDesktopDark(Shell *shell, bool dark) {
  rigOf(shell).shell.appearance()->setDesktopScheme(
      dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
}

double Harness::lastPlayedFrom(Shell *shell) {
  const Rig &rig = rigOf(shell);
  if (rig.playback.playedFrom.empty()) {
    return -1.0;
  }
  return static_cast<double>(rig.playback.playedFrom.back().ticks) /
         static_cast<double>(model::ticksPerQuarter);
}

void Harness::setReducedMotion(Shell *shell, bool reduced) {
  rigOf(shell).shell.appearance()->setDesktopMotion(
      reduced ? Qt::MotionPreference::ReducedMotion
              : Qt::MotionPreference::NoPreference);
}

} // namespace lina::ui::testing
