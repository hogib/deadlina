// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_APP_OPTIONS_HPP
#define LINA_APP_OPTIONS_HPP

#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

namespace lina::app {

/// What the command line and the environment ask for.
struct Options {
  /// False with --no-audio, or with LINA_NO_AUDIO set to anything but
  /// empty or "0": no output is opened, and the transport cannot play.
  /// Automated runs use it so that the machine never makes a sound.
  bool audio{true};
  /// --quit-after-start: quit once the window has drawn its first frame.
  /// For the start-up test.
  bool quitAfterStart{false};
  /// A project folder to open.
  std::optional<QString> project;
  /// -h or --help: print `helpText` and stop.
  bool showHelp{false};
  QString helpText;
  /// -v or --version: print the version and stop.
  bool showVersion{false};
};

/// Reads the arguments (the first is the program) and the value of
/// LINA_NO_AUDIO. An error is a message for the user.
[[nodiscard]] std::expected<Options, QString>
parseOptions(const QStringList &arguments, const QString &noAudioVariable);

} // namespace lina::app

#endif // LINA_APP_OPTIONS_HPP
