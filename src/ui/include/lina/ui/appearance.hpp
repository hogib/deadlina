// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_APPEARANCE_HPP
#define LINA_UI_APPEARANCE_HPP

#include <QColor>
#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

#include <array>
#include <cstdint>

namespace lina::ui {

/// The colours of the interface, by what they are for. These two sets are
/// the only place colours are defined (docs/design/01-clean-room.md: the
/// palette is LINA's own).
struct ColourTokens {
  /// Behind everything.
  QColor window;
  /// Panes and bars.
  QColor surface;
  /// Things that sit on a surface: fields, buttons, list rows.
  QColor raised;
  QColor text;
  /// Secondary text: hints, units, the help area's explanation.
  QColor mutedText;
  QColor divider;
  /// What is active or chosen: a playing transport, the current row.
  QColor accent;
  /// Text and symbols on the accent colour.
  QColor onAccent;
  /// The mark around the pane and control that have the keys.
  QColor focus;
  QColor warning;
  QColor error;
};

[[nodiscard]] const ColourTokens &lightColours();
[[nodiscard]] const ColourTokens &darkColours();

/// WCAG 2 contrast ratio between two opaque colours, from 1 to 21.
[[nodiscard]] double contrastRatio(const QColor &a, const QColor &b);

/// Light or dark, the size of everything, and how much moves
/// (docs/design/11-non-functional-requirements.md, "Accessibility and
/// display").
///
/// Light or dark follows the desktop unless the user picks one. The size is
/// a factor the user sets on top of the display's own scale, which Qt
/// applies, fractional factors included. Animations are switched off when
/// the desktop asks for reduced motion.
class Appearance final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the shell")
  Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
  Q_PROPERTY(bool dark READ dark NOTIFY coloursChanged FINAL)
  Q_PROPERTY(double scale READ scale NOTIFY scaleChanged FINAL)
  /// The grid that sizes and spacing are counted in: 8 logical pixels at
  /// the usual size. Qt turns logical pixels into the display's own.
  Q_PROPERTY(double unit READ unit NOTIFY scaleChanged FINAL)
  /// The desktop's font size times the scale.
  Q_PROPERTY(double fontPointSize READ fontPointSize NOTIFY scaleChanged FINAL)
  Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY motionChanged FINAL)
  /// Length of the interface's short animations in milliseconds; 0 with
  /// reduced motion.
  Q_PROPERTY(int animationMs READ animationMs NOTIFY motionChanged FINAL)
  Q_PROPERTY(QColor window READ window NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor surface READ surface NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor raised READ raised NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor text READ text NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor mutedText READ mutedText NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor divider READ divider NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor accent READ accent NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor onAccent READ onAccent NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor focus READ focus NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor warning READ warning NOTIFY coloursChanged FINAL)
  Q_PROPERTY(QColor error READ error NOTIFY coloursChanged FINAL)

public:
  enum class Mode : std::uint8_t { FollowDesktop, Light, Dark };
  Q_ENUM(Mode)

  /// The sizes the user can step through; 1 is the desktop's own size.
  static constexpr std::array<double, 9> scaleSteps{
      0.75, 0.875, 1.0, 1.125, 1.25, 1.5, 1.75, 2.0, 2.5,
  };
  static constexpr int animationLength = 120;
  static constexpr double baseUnit = 8.0;
  /// Used when the desktop gives its font size in pixels, or none.
  static constexpr double fallbackPointSize = 10.0;

  /// Starts from the desktop's settings as Qt reports them, and follows
  /// them while they change.
  explicit Appearance(QObject *parent = nullptr);

  [[nodiscard]] Mode mode() const { return mode_; }
  void setMode(Mode mode);
  [[nodiscard]] bool dark() const;

  /// What the desktop asks for. Set from Qt's style hints; tests set it.
  void setDesktopScheme(Qt::ColorScheme scheme);
  void setDesktopMotion(Qt::MotionPreference motion);

  [[nodiscard]] double scale() const;
  [[nodiscard]] double unit() const { return baseUnit * scale(); }
  [[nodiscard]] double fontPointSize() const;
  Q_INVOKABLE void larger();
  Q_INVOKABLE void smaller();
  Q_INVOKABLE void resetScale();
  [[nodiscard]] bool canGrow() const;
  [[nodiscard]] bool canShrink() const;

  [[nodiscard]] bool reducedMotion() const;
  [[nodiscard]] int animationMs() const;

  [[nodiscard]] const ColourTokens &colours() const;
  [[nodiscard]] QColor window() const { return colours().window; }
  [[nodiscard]] QColor surface() const { return colours().surface; }
  [[nodiscard]] QColor raised() const { return colours().raised; }
  [[nodiscard]] QColor text() const { return colours().text; }
  [[nodiscard]] QColor mutedText() const { return colours().mutedText; }
  [[nodiscard]] QColor divider() const { return colours().divider; }
  [[nodiscard]] QColor accent() const { return colours().accent; }
  [[nodiscard]] QColor onAccent() const { return colours().onAccent; }
  [[nodiscard]] QColor focus() const { return colours().focus; }
  [[nodiscard]] QColor warning() const { return colours().warning; }
  [[nodiscard]] QColor error() const { return colours().error; }

Q_SIGNALS:
  void modeChanged();
  void coloursChanged();
  void scaleChanged();
  void motionChanged();

private:
  void setScaleStep(std::size_t step);

  Mode mode_{Mode::FollowDesktop};
  Qt::ColorScheme desktopScheme_{Qt::ColorScheme::Unknown};
  Qt::MotionPreference desktopMotion_{Qt::MotionPreference::NoPreference};
  std::size_t scaleStep_{2};
};

} // namespace lina::ui

#endif // LINA_UI_APPEARANCE_HPP
