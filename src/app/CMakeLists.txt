# SPDX-License-Identifier: GPL-3.0-or-later
#
# lina_app: start-up, shutdown and the command line (docs/design/13-architecture.md), as a
# library so that tests can bring the whole application up; and `lina`, the executable, which
# is main() and nothing else.
lina_add_library(lina_app
  SOURCES
    application.cpp
    options.cpp
  PUBLIC_LIBRARIES
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    lina::bridge
    lina::engine
    lina::format
    lina::ui
    lina_uiplugin
    lina_shellplugin)
target_compile_definitions(lina_app PRIVATE LINA_VERSION="${PROJECT_VERSION}")

add_executable(lina main.cpp)
lina_target_defaults(lina)
target_link_libraries(lina PRIVATE lina::app)
