// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface test tier (docs/design/14-build-test-and-ci.md): the real
// QML of the main window, driven by synthesized keys and mouse in an
// offscreen window, over shells with a fake engine.
#include "lina/ui/testing/headless.hpp"

#include <QObject>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

#include <unistd.h>

#include <string>

Q_IMPORT_QML_PLUGIN(Lina_UiPlugin)
Q_IMPORT_QML_PLUGIN(Lina_ShellPlugin)
Q_IMPORT_QML_PLUGIN(LinaTestPlugin)

namespace {

/// Made before the Qt application: the environment is set while the
/// process has one thread. Each test file runs in a process of its own, and
/// those run side by side, so the scratch folder is named after the process.
class Setup : public QObject {
  Q_OBJECT

public:
  Setup() {
    lina::ui::testing::enterHeadless("qml-tests-" + std::to_string(::getpid()));
  }
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(lina_qml_tests, Setup)

#include "qml_tests_main.moc"
