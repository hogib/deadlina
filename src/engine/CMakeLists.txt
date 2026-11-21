# SPDX-License-Identifier: GPL-3.0-or-later
#
# lina_engine: the playback engine behind LINA-only headers
# (docs/design/13-architecture.md). Tracktion and JUCE are linked privately, so their headers
# and compile definitions do not reach targets that link lina::engine.
lina_add_library(lina_engine
  SOURCES
    audio_parts.cpp
    engine.cpp
    engine_plugins.cpp
    message_thread.cpp
    midi_events.cpp
    mixing.cpp
    note_guard.cpp
    projection.cpp
    time_map.cpp
  PUBLIC_LIBRARIES
    lina::model
  PRIVATE_LIBRARIES
    lina::tracktion)
target_include_directories(lina_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal")

# For code inside the engine tier and its tests only: what has to reach the Tracktion objects
# behind an Engine, such as tests that build an edit by hand and the test instrument and
# effect. It hands out Tracktion's and JUCE's headers, so nothing above the engine (bridge,
# interface, application) may link it.
add_library(lina_engine_internals INTERFACE)
add_library(lina::engine_internals ALIAS lina_engine_internals)
target_include_directories(lina_engine_internals INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/internal")
target_link_libraries(lina_engine_internals INTERFACE lina::engine lina::tracktion)
