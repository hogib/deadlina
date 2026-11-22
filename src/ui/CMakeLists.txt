# SPDX-License-Identifier: GPL-3.0-or-later
#
# lina_ui: the interface's C++ side (docs/design/13-architecture.md): the document controller,
# the transport, the command registry and its list, thin models over the document, the layout
# and the appearance. Qt only: it reaches the engine through lina_bridge's Playback and never
# sees a JUCE or Tracktion type. Its types form the QML module Lina.Ui; the QML itself is in
# qml/ (module Lina.Shell).
#
# Headers are listed with the sources so that moc and QML type registration see them.
lina_add_library(lina_ui
  SOURCES
    appearance.cpp
    command_palette.cpp
    command_registry.cpp
    document_controller.cpp
    fault_text.cpp
    prompt.cpp
    shell.cpp
    shortcut_dispatcher.cpp
    track_list_model.cpp
    transport_controller.cpp
    units.cpp
    workspace.cpp
    include/lina/ui/appearance.hpp
    include/lina/ui/command_palette.hpp
    include/lina/ui/command_registry.hpp
    include/lina/ui/document_controller.hpp
    include/lina/ui/fault_text.hpp
    include/lina/ui/prompt.hpp
    include/lina/ui/shell.hpp
    include/lina/ui/shortcut_dispatcher.hpp
    include/lina/ui/track_list_model.hpp
    include/lina/ui/transport_controller.hpp
    include/lina/ui/units.hpp
    include/lina/ui/workspace.hpp
  PUBLIC_LIBRARIES
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    lina::bridge
    lina::format
    lina::model)
set_target_properties(lina_ui PROPERTIES AUTOMOC ON)
# The generated type registration includes each header by its bare name.
target_include_directories(lina_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/lina/ui")

qt_add_qml_module(lina_ui
  URI Lina.Ui
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml)
