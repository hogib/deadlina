// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/app/options.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

namespace lina::app {

std::expected<Options, QString> parseOptions(const QStringList &arguments,
                                             const QString &noAudioVariable) {
  QCommandLineParser parser;
  parser.setApplicationDescription(QCoreApplication::translate(
      "lina::app", "LINA, a digital audio workstation."));
  const QCommandLineOption help = parser.addHelpOption();
  const QCommandLineOption version = parser.addVersionOption();
  const QCommandLineOption noAudio(
      QStringLiteral("no-audio"),
      QCoreApplication::translate(
          "lina::app", "Open no audio output; the transport cannot play."));
  QCommandLineOption quitAfterStart(
      QStringLiteral("quit-after-start"),
      QCoreApplication::translate(
          "lina::app", "Quit as soon as the window has been drawn once."));
  quitAfterStart.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(noAudio);
  parser.addOption(quitAfterStart);
  parser.addPositionalArgument(
      QStringLiteral("project"),
      QCoreApplication::translate("lina::app", "A project folder to open."),
      QStringLiteral("[project]"));

  if (!parser.parse(arguments)) {
    return std::unexpected(parser.errorText());
  }
  const QStringList positional = parser.positionalArguments();
  if (positional.size() > 1) {
    return std::unexpected(QCoreApplication::translate(
        "lina::app", "Give at most one project folder."));
  }

  Options options;
  options.showHelp = parser.isSet(help);
  options.helpText = parser.helpText();
  options.showVersion = parser.isSet(version);
  options.audio =
      !parser.isSet(noAudio) &&
      (noAudioVariable.isEmpty() || noAudioVariable == QStringLiteral("0"));
  options.quitAfterStart = parser.isSet(quitAfterStart);
  if (!positional.isEmpty()) {
    options.project = positional.front();
  }
  return options;
}

} // namespace lina::app
