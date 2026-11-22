# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lina.Shell: the QML of the main window (docs/design/12-interaction-principles.md). Layout
# and bindings only; every decision is made by the C++ types of Lina.Ui. The files are compiled
# into the library and read from its resources, never from the source tree.
qt_add_library(lina_shell STATIC)
set_target_properties(lina_shell PROPERTIES AUTOMOC ON)
qt_add_qml_module(lina_shell
  URI Lina.Shell
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  IMPORTS Lina.Ui
  QML_FILES
    ArrangementArea.qml
    CommandListPopup.qml
    HelpBar.qml
    Main.qml
    MainView.qml
    NoticeBar.qml
    PaneFrame.qml
    PromptDialog.qml
    SessionArea.qml
    ShellButton.qml
    SplitDivider.qml
    TempoField.qml
    TransportBar.qml)
target_link_libraries(lina_shell PRIVATE Qt6::Quick Qt6::QuickControls2)
