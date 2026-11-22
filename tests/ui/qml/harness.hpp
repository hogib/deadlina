// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_TESTS_QML_HARNESS_HPP
#define LINA_UI_TESTS_QML_HARNESS_HPP

#include "lina/format/file_system.hpp"
#include "lina/ui/shell.hpp"
#include "lina/ui/testing/fake_playback.hpp"

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <vector>

namespace lina::ui::testing {

/// What the QML tests reach beyond the shell: fresh shells over a fake
/// engine, and the parts of the world a test plays (the engine's position,
/// whether there is an output, what the desktop asks for).
class Harness : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

public:
  explicit Harness(QObject *parent = nullptr);
  ~Harness() override;

  Harness(const Harness &) = delete;
  Harness &operator=(const Harness &) = delete;
  Harness(Harness &&) = delete;
  Harness &operator=(Harness &&) = delete;

  /// A shell of its own for one test function. It lives as long as the
  /// harness; QML never owns it.
  Q_INVOKABLE lina::ui::Shell *makeShell();

  Q_INVOKABLE void setOutput(lina::ui::Shell *shell, bool output);
  /// Where the fake engine says it is, in quarter notes.
  Q_INVOKABLE void setEnginePosition(lina::ui::Shell *shell, int quarters);
  Q_INVOKABLE [[nodiscard]] bool enginePlaying(lina::ui::Shell *shell);
  Q_INVOKABLE void setDesktopDark(lina::ui::Shell *shell, bool dark);
  Q_INVOKABLE void setReducedMotion(lina::ui::Shell *shell, bool reduced);
  /// Where the fake engine was last told to start, in quarter notes; -1
  /// when it never was.
  Q_INVOKABLE [[nodiscard]] double lastPlayedFrom(lina::ui::Shell *shell);

private:
  struct Rig {
    FakePlayback playback;
    format::PosixFileSystem files;
    Shell shell{playback, files, "LINA test"};
  };

  Rig &rigOf(const Shell *shell);

  std::vector<std::unique_ptr<Rig>> rigs_;
};

} // namespace lina::ui::testing

#endif // LINA_UI_TESTS_QML_HARNESS_HPP
