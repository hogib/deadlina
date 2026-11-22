// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/app/application.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>
#include <stdexcept>

// The QML modules are linked in, with their files in the executable's
// resources.
Q_IMPORT_QML_PLUGIN(Lina_UiPlugin)
Q_IMPORT_QML_PLUGIN(Lina_ShellPlugin)

namespace lina::app {

namespace {

QString writtenBy() {
  return QStringLiteral("LINA %1").arg(QCoreApplication::applicationVersion());
}

std::chrono::nanoseconds steadyNow() {
  return std::chrono::steady_clock::now().time_since_epoch();
}

} // namespace

Application::Application(const Options &options,
                         const std::filesystem::path &storage)
    : engine_({.storageFolder = storage}), playback_(engine_),
      shell_(playback_, files_, writtenBy()),
      qml_(std::make_unique<QQmlApplicationEngine>()) {
  // exit(), not quit(): quit() first asks the windows to close, and the
  // window's answer is to ask the document controller, which has already
  // approved.
  QObject::connect(
      shell_.document(), &ui::DocumentController::quitApproved,
      QCoreApplication::instance(), [] { QCoreApplication::exit(0); },
      Qt::QueuedConnection);

  qml_->setInitialProperties({{"shell", QVariant::fromValue(&shell_)}});
  qml_->loadFromModule("Lina.Shell", "Main");
  if (window() == nullptr) {
    throw std::runtime_error("the main window could not be made");
  }

  if (options.audio) {
    openAudioOutput();
  }
  if (options.project) {
    shell_.document()->openFolder(
        std::filesystem::path(options.project->toStdString()));
  }
  if (options.quitAfterStart) {
    QObject::connect(
        window(), &QQuickWindow::frameSwapped, QCoreApplication::instance(),
        [] { QCoreApplication::exit(0); }, Qt::QueuedConnection);
  }
  // Shown last, when everything it shows is in place.
  window()->show();
}

Application::~Application() {
  // Sound stops before anything else goes.
  closeAudioOutput();
  qml_.reset();
}

void Application::closeAudioOutput() {
  playback_.closeOutput();
  shell_.transport()->refreshOutput();
}

QQuickWindow *Application::window() const {
  const auto roots = qml_->rootObjects();
  return roots.isEmpty() ? nullptr
                         : qobject_cast<QQuickWindow *>(roots.front());
}

void Application::openAudioOutput() {
  playback_.openSilentOutput({.sampleRate = 48000.0, .blockSize = 512},
                             steadyNow);
  shell_.transport()->refreshOutput();
}

int run(int argc, char **argv) {
  const QGuiApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("lina"));
  QCoreApplication::setApplicationVersion(QStringLiteral(LINA_VERSION));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("LINA"));
  QGuiApplication::setDesktopFileName(QStringLiteral("lina"));

  const auto options = parseOptions(QCoreApplication::arguments(),
                                    qEnvironmentVariable("LINA_NO_AUDIO"));
  if (!options) {
    std::println(stderr, "lina: {}", options.error().toStdString());
    return EXIT_FAILURE;
  }
  if (options->showHelp || options->showVersion) {
    std::print("{}", options->showHelp ? options->helpText.toStdString()
                                       : writtenBy().toStdString() + "\n");
    return EXIT_SUCCESS;
  }
  try {
    const std::filesystem::path storage =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            .toStdString();
    const Application lina(*options, storage / "engine");
    return QCoreApplication::exec();
  } catch (const std::exception &error) {
    std::println(stderr, "lina: {}", error.what());
    return EXIT_FAILURE;
  }
}

} // namespace lina::app
