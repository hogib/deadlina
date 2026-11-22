# SPDX-License-Identifier: GPL-3.0-or-later
#
# Qt, from system packages (docs/design/14-build-test-and-ci.md). Found once here so that every
# tier from the bridge upwards, and their tests, see the same imported targets.
find_package(Qt6 6.12 REQUIRED
  COMPONENTS Core Gui Qml Quick QuickControls2 QuickTest Test)

# Every QML module of LINA's own lands under one folder of the build, which is then the one
# import path that the QML tools and the tests need. The modules are compiled into the
# executables; nothing is read from this folder or from the source tree at run time.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
