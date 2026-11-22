// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/appearance.hpp"

#include <QAccessibilityHints>
#include <QFont>
#include <QGuiApplication>
#include <QStyleHints>

#include <algorithm>
#include <cmath>

namespace lina::ui {

namespace {

// LINA's own palette: warm paper and graphite, a teal accent, and a violet
// focus mark that differs from every other colour in hue, so that the pane
// with the keys stands out in both appearances.
ColourTokens makeLight() {
  return {
      .window = QColor(0xEC, 0xE8, 0xDF),
      .surface = QColor(0xF6, 0xF3, 0xEC),
      .raised = QColor(0xFF, 0xFF, 0xFF),
      .text = QColor(0x1E, 0x22, 0x26),
      .mutedText = QColor(0x55, 0x5C, 0x63),
      .divider = QColor(0xC9, 0xC2, 0xB4),
      .accent = QColor(0x0E, 0x6E, 0x62),
      .onAccent = QColor(0xFF, 0xFF, 0xFF),
      .focus = QColor(0x63, 0x4F, 0xC9),
      .warning = QColor(0x8A, 0x4B, 0x00),
      .error = QColor(0xA8, 0x1F, 0x1F),
  };
}

ColourTokens makeDark() {
  return {
      .window = QColor(0x14, 0x16, 0x19),
      .surface = QColor(0x1C, 0x1F, 0x23),
      .raised = QColor(0x26, 0x2A, 0x2F),
      .text = QColor(0xE9, 0xE6, 0xE0),
      .mutedText = QColor(0xA3, 0xA7, 0xAD),
      .divider = QColor(0x3A, 0x3F, 0x46),
      .accent = QColor(0x46, 0xBF, 0xAC),
      .onAccent = QColor(0x0C, 0x11, 0x12),
      .focus = QColor(0xAE, 0xA0, 0xF7),
      .warning = QColor(0xF0, 0xB0, 0x5A),
      .error = QColor(0xF2, 0x82, 0x7C),
  };
}

double linear(double channel) {
  constexpr double threshold = 0.04045;
  constexpr double low = 12.92;
  constexpr double offset = 0.055;
  constexpr double scale = 1.055;
  constexpr double power = 2.4;
  return channel <= threshold ? channel / low
                              : std::pow((channel + offset) / scale, power);
}

double luminance(const QColor &colour) {
  constexpr double red = 0.2126;
  constexpr double green = 0.7152;
  constexpr double blue = 0.0722;
  return (red * linear(colour.redF())) + (green * linear(colour.greenF())) +
         (blue * linear(colour.blueF()));
}

} // namespace

const ColourTokens &lightColours() {
  static const ColourTokens colours = makeLight();
  return colours;
}

const ColourTokens &darkColours() {
  static const ColourTokens colours = makeDark();
  return colours;
}

double contrastRatio(const QColor &a, const QColor &b) {
  constexpr double flare = 0.05;
  const double first = luminance(a) + flare;
  const double second = luminance(b) + flare;
  return std::max(first, second) / std::min(first, second);
}

Appearance::Appearance(QObject *parent) : QObject(parent) {
  if (qGuiApp == nullptr) {
    return;
  }
  const QStyleHints *hints = QGuiApplication::styleHints();
  desktopScheme_ = hints->colorScheme();
  desktopMotion_ = hints->accessibility()->motionPreference();
  connect(hints, &QStyleHints::colorSchemeChanged, this,
          &Appearance::setDesktopScheme);
  connect(hints->accessibility(), &QAccessibilityHints::motionPreferenceChanged,
          this, &Appearance::setDesktopMotion);
}

void Appearance::setMode(Mode mode) {
  if (mode == mode_) {
    return;
  }
  const bool wasDark = dark();
  mode_ = mode;
  Q_EMIT modeChanged();
  if (dark() != wasDark) {
    Q_EMIT coloursChanged();
  }
}

bool Appearance::dark() const {
  if (mode_ == Mode::FollowDesktop) {
    return desktopScheme_ == Qt::ColorScheme::Dark;
  }
  return mode_ == Mode::Dark;
}

void Appearance::setDesktopScheme(Qt::ColorScheme scheme) {
  const bool wasDark = dark();
  desktopScheme_ = scheme;
  if (dark() != wasDark) {
    Q_EMIT coloursChanged();
  }
}

void Appearance::setDesktopMotion(Qt::MotionPreference motion) {
  if (motion != desktopMotion_) {
    desktopMotion_ = motion;
    Q_EMIT motionChanged();
  }
}

double Appearance::scale() const { return scaleSteps.at(scaleStep_); }

void Appearance::setScaleStep(std::size_t step) {
  step = std::min(step, scaleSteps.size() - 1);
  if (step != scaleStep_) {
    scaleStep_ = step;
    Q_EMIT scaleChanged();
  }
}

void Appearance::larger() { setScaleStep(scaleStep_ + 1); }

void Appearance::smaller() {
  if (scaleStep_ > 0) {
    setScaleStep(scaleStep_ - 1);
  }
}

void Appearance::resetScale() {
  const auto *const one = std::ranges::find(scaleSteps, 1.0);
  setScaleStep(static_cast<std::size_t>(one - scaleSteps.begin()));
}

bool Appearance::canGrow() const { return scaleStep_ + 1 < scaleSteps.size(); }

bool Appearance::canShrink() const { return scaleStep_ > 0; }

double Appearance::fontPointSize() const {
  const double desktop =
      qGuiApp != nullptr ? QGuiApplication::font().pointSizeF() : 0.0;
  return (desktop > 0.0 ? desktop : fallbackPointSize) * scale();
}

bool Appearance::reducedMotion() const {
  return desktopMotion_ == Qt::MotionPreference::ReducedMotion;
}

int Appearance::animationMs() const {
  return reducedMotion() ? 0 : animationLength;
}

const ColourTokens &Appearance::colours() const {
  return dark() ? darkColours() : lightColours();
}

} // namespace lina::ui
