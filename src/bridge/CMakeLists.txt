# SPDX-License-Identifier: GPL-3.0-or-later
#
# lina_bridge: where Qt meets the engine (docs/design/13-architecture.md). It holds the
# event-loop integration, which is the one place that includes both Qt and JUCE headers, and
# the interface's view of the engine (Playback, EnginePlayback), whose headers show LINA and Qt
# types only.

lina_add_library(lina_bridge
  SOURCES
    engine_playback.cpp
    event_loop_bridge.cpp
    # Listed so that moc sees it.
    include/lina/bridge/engine_playback.hpp
  PUBLIC_LIBRARIES
    Qt6::Core
    lina::engine
  PRIVATE_LIBRARIES
    lina::tracktion)
set_target_properties(lina_bridge PROPERTIES AUTOMOC ON)

# Tracktion has identifiers named `slots` (docs/design/14-build-test-and-ci.md).
target_compile_definitions(lina_bridge PUBLIC QT_NO_KEYWORDS)
