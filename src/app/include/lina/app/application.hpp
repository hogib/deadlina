// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_APP_APPLICATION_HPP
#define LINA_APP_APPLICATION_HPP

#include "lina/app/options.hpp"
#include "lina/bridge/engine_playback.hpp"
#include "lina/bridge/event_loop_bridge.hpp"
#include "lina/engine/engine.hpp"
#include "lina/engine/message_thread.hpp"
#include "lina/format/file_system.hpp"
#include "lina/ui/shell.hpp"

#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include <filesystem>
#include <memory>

namespace lina::app {

/// The application's object graph, built and taken down in the order of
/// docs/design/13-architecture.md, "Start-up and shutdown":
///
///   start-up:  (the Qt application, made by the caller), the engine
///              framework's message thread on the main thread, the
///              event-loop bridge, the engine, the shell, the window
///              (QML); then the audio output, as a step of its own that
///              --no-audio skips; then the project from the command line.
///   shutdown:  the audio output is closed first, then the window, the
///              shell, the engine, the bridge and the message thread, in
///              that order; the Qt application goes last, with the caller.
class Application {
public:
  /// A QGuiApplication must exist. `storage` is the folder for the
  /// engine's settings and caches. Throws std::runtime_error when the
  /// window cannot be made.
  Application(const Options &options, const std::filesystem::path &storage);
  ~Application();

  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;
  Application(Application &&) = delete;
  Application &operator=(Application &&) = delete;

  [[nodiscard]] ui::Shell &shell() { return shell_; }
  [[nodiscard]] QQuickWindow *window() const;
  [[nodiscard]] bool hasAudioOutput() const { return playback_.hasOutput(); }

  /// Stops playback and closes the audio output: the first step of
  /// shutdown, which the destructor takes if it has not been taken.
  void closeAudioOutput();

private:
  /// The separate, explicit step that makes sound possible. The engine can
  /// open no sound device of the machine yet, so this opens its silent
  /// output (EnginePlayback), which keeps time and is heard nowhere.
  void openAudioOutput();

  engine::MessageThread messageThread_;
  bridge::EventLoopBridge eventLoopBridge_;
  engine::Engine engine_;
  bridge::EnginePlayback playback_;
  format::PosixFileSystem files_;
  ui::Shell shell_;
  std::unique_ptr<QQmlApplicationEngine> qml_;
};

/// main(): the Qt application, the options, the Application, the event
/// loop. Returns the process's exit status.
int run(int argc, char **argv);

} // namespace lina::app

#endif // LINA_APP_APPLICATION_HPP
