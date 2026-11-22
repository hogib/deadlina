// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_TESTING_HEADLESS_HPP
#define LINA_UI_TESTING_HEADLESS_HPP

#include <QGuiApplication>

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string_view>

namespace lina::ui::testing {

/// An empty scratch folder for one test case (test cases run side by side,
/// so each needs a name of its own), and an environment that keeps the
/// test headless and inside it, also when the test binary is run by hand:
/// Qt on its offscreen platform with the scene graph's real (RHI) renderer
/// on its null backend, HOME and TMPDIR in the folder, no DISPLAY. Call it
/// first in a test case, while the process has one thread.
std::filesystem::path enterHeadless(std::string_view name);

/// A QGuiApplication with the argument vector it refers to.
struct GuiApplication {
  int argc = 1;
  std::array<char, 8> name{"lina-ui"};
  std::array<char *, 2> argv{name.data(), nullptr};
  QGuiApplication app{argc, argv.data()};
};

/// Blocks in Qt's event loop until `done` returns true, and returns whether
/// it did before the deadline. The deadline is far longer than anything
/// waited for takes; it is reached only by a failing test.
bool spinUntil(const std::function<bool()> &done,
               std::chrono::milliseconds timeout = std::chrono::seconds(20));

} // namespace lina::ui::testing

#endif // LINA_UI_TESTING_HEADLESS_HPP
