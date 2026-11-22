# SPDX-License-Identifier: GPL-3.0-or-later

get_target_property(lina_bridge_test_scratch lina_engine_testing LINA_TEST_SCRATCH_DIR)

# The tests run a Qt application without a display server: Qt on its offscreen platform, which
# the tests select themselves (QT_QPA_PLATFORM), and JUCE with no X server to connect to. They
# fail when JUCE reports a failed assertion or a leaked object.
lina_add_catch_test(lina_bridge_event_loop_test
  SOURCES event_loop_bridge_test.cpp
  LIBRARIES lina::bridge lina::engine_testing Qt6::Gui
  PROPERTIES
    FAIL_REGULAR_EXPRESSION "JUCE Assertion failure|Leaked objects detected"
    ENVIRONMENT "TMPDIR=${lina_bridge_test_scratch}/tmp"
    ENVIRONMENT_MODIFICATION "DISPLAY=unset:"
    # A test that waits for an event that never comes fails on its own deadline first.
    TIMEOUT 120)

# The interface's view of the engine, with a manual device and a clock of the test's own.
lina_add_catch_test(lina_bridge_engine_playback_test
  SOURCES engine_playback_test.cpp
  LIBRARIES lina::bridge lina::engine_testing
  PROPERTIES
    FAIL_REGULAR_EXPRESSION "JUCE Assertion failure|Leaked objects detected"
    ENVIRONMENT "TMPDIR=${lina_bridge_test_scratch}/tmp"
    ENVIRONMENT_MODIFICATION "DISPLAY=unset:"
    TIMEOUT 120)
