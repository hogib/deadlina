// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/testing/headless.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QTimer>

#include <cstdlib>
#include <stdexcept>

namespace lina::ui::testing {

std::filesystem::path enterHeadless(std::string_view name) {
  const auto folder = std::filesystem::path(LINA_UI_TEST_SCRATCH_DIR) / name;
  std::filesystem::remove_all(folder);
  const auto temporary = folder / "tmp";
  std::filesystem::create_directories(temporary);
  // NOLINTBEGIN(concurrency-mt-unsafe)
  const bool set = ::setenv("HOME", folder.c_str(), 1) == 0 &&
                   ::setenv("TMPDIR", temporary.c_str(), 1) == 0 &&
                   ::setenv("XDG_RUNTIME_DIR", temporary.c_str(), 1) == 0 &&
                   ::setenv("QT_QPA_PLATFORM", "offscreen", 1) == 0 &&
                   ::setenv("QT_QUICK_BACKEND", "rhi", 1) == 0 &&
                   ::setenv("QSG_RHI_BACKEND", "null", 1) == 0 &&
                   ::setenv("QT_FORCE_STDERR_LOGGING", "1", 1) == 0 &&
                   ::unsetenv("DISPLAY") == 0 &&
                   ::unsetenv("WAYLAND_DISPLAY") == 0;
  // NOLINTEND(concurrency-mt-unsafe)
  if (!set) {
    throw std::runtime_error("could not set up the test environment");
  }
  return folder;
}

bool spinUntil(const std::function<bool()> &done,
               std::chrono::milliseconds timeout) {
  const QDeadlineTimer deadline(timeout);
  // Wakes the loop so that the deadline is looked at even when nothing
  // else arrives.
  QTimer wake;
  wake.start(std::chrono::milliseconds(20));
  while (!done() && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
  }
  return done();
}

} // namespace lina::ui::testing
