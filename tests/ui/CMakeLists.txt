# SPDX-License-Identifier: GPL-3.0-or-later
#
# Interface tier (docs/design/14-build-test-and-ci.md): the C++ of lina_ui and lina_app with
# Catch2, the QML with Qt Quick Test, and the application itself started once. Everything runs
# headless: Qt's offscreen platform with the scene graph's RHI renderer on its null backend
# (the offscreen platform has no OpenGL without a display server), no audio output, and files
# only under the build directory's scratch folder.
set(lina_ui_test_scratch "${CMAKE_BINARY_DIR}/tests/ui-scratch")
file(MAKE_DIRECTORY "${lina_ui_test_scratch}")
set(lina_ui_test_environment
  "QT_QPA_PLATFORM=offscreen"
  "QT_QUICK_BACKEND=rhi"
  "QSG_RHI_BACKEND=null"
  "QT_FORCE_STDERR_LOGGING=1"
  "TMPDIR=${lina_ui_test_scratch}")
# Read by ThreadSanitizer only; see the file.
set(lina_ui_tsan_suppressions
  "TSAN_OPTIONS=string_append::suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan-suppressions.txt")
set(lina_ui_test_environment_modification
  "DISPLAY=unset:" "WAYLAND_DISPLAY=unset:" "${lina_ui_tsan_suppressions}")
# A QML error or warning, a JUCE assertion or a leaked JUCE object fails a test even when the
# test itself passed.
set(lina_ui_test_failures
  "JUCE Assertion failure|Leaked objects detected|qrc:/.*: |file:///.*\\.qml:[0-9]+")

# catch_discover_tests cannot pass a list as a property value. Its tests set up Qt's headless
# environment themselves (lina::ui::testing::enterHeadless); what has to be in place before
# the process starts is the sanitizer's suppression file alone.

add_library(lina_ui_test_support STATIC
  support/fake_playback.cpp
  support/headless.cpp
  support/scripted_file_system.cpp)
add_library(lina::ui_test_support ALIAS lina_ui_test_support)
lina_target_defaults(lina_ui_test_support)
target_include_directories(lina_ui_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support/include")
target_link_libraries(lina_ui_test_support PUBLIC lina::ui Qt6::Gui Qt6::Test)
target_compile_definitions(lina_ui_test_support PRIVATE
  LINA_UI_TEST_SCRATCH_DIR="${lina_ui_test_scratch}")

lina_add_catch_test(lina_ui_tests
  SOURCES
    commands_test.cpp
    document_controller_test.cpp
    models_test.cpp
    project_files_test.cpp
    shell_test.cpp
  LIBRARIES lina::ui_test_support lina::model_test_support Qt6::Qml Qt6::Quick
  PROPERTIES
    ENVIRONMENT_MODIFICATION "${lina_ui_tsan_suppressions}"
    FAIL_REGULAR_EXPRESSION "${lina_ui_test_failures}"
    # Bringing up a QML engine takes about a minute under ThreadSanitizer on a loaded machine.
    TIMEOUT 300)

# The whole application, in process: Qt, JUCE, the bridge, the engine and the window.
lina_add_catch_test(lina_app_tests
  SOURCES application_test.cpp
  LIBRARIES lina::app lina::ui_test_support lina::engine_testing
  PROPERTIES
    ENVIRONMENT_MODIFICATION "${lina_ui_tsan_suppressions}"
    FAIL_REGULAR_EXPRESSION "${lina_ui_test_failures}"
    # Bringing up a QML engine takes about a minute under ThreadSanitizer on a loaded machine.
    TIMEOUT 300)

# The QML tests. LinaTest is a QML module of the tests' own: fresh shells over a fake engine.
qt_add_library(lina_qml_test_support STATIC)
set_target_properties(lina_qml_test_support PROPERTIES AUTOMOC ON)
qt_add_qml_module(lina_qml_test_support
  URI LinaTest
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  SOURCES qml/harness.cpp qml/harness.hpp)
lina_target_defaults(lina_qml_test_support)
target_include_directories(lina_qml_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/qml")
target_link_libraries(lina_qml_test_support PUBLIC lina::ui_test_support)

add_executable(lina_qml_tests qml/qml_tests_main.cpp)
set_target_properties(lina_qml_tests PROPERTIES AUTOMOC ON)
lina_target_defaults(lina_qml_tests)
target_link_libraries(lina_qml_tests PRIVATE
  lina::ui_test_support lina_qml_test_supportplugin lina_uiplugin lina_shellplugin
  Qt6::QuickTest Qt6::Quick Qt6::QuickControls2)
# Qt Quick Test reads the test files from here at run time; the application reads nothing
# from the source tree.
target_compile_definitions(lina_qml_tests PRIVATE
  QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
# One process per test file: each file brings up QML engines of its own, and keeping them apart
# keeps a failure in one file from hiding the others. (It also keeps ThreadSanitizer from
# seeing a second QML loader thread reuse the identity of one that Qt did not join.)
file(GLOB lina_qml_test_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_*.qml")
foreach(test_file IN LISTS lina_qml_test_files)
  get_filename_component(test_name "${test_file}" NAME_WE)
  string(REGEX REPLACE "^tst_" "" test_name "${test_name}")
  add_test(NAME "qml: ${test_name}" COMMAND lina_qml_tests -input "${test_file}")
  set_tests_properties("qml: ${test_name}" PROPERTIES
    ENVIRONMENT "${lina_ui_test_environment}"
    ENVIRONMENT_MODIFICATION "${lina_ui_test_environment_modification}"
    FAIL_REGULAR_EXPRESSION "${lina_ui_test_failures}"
    TIMEOUT 300)
endforeach()
set_property(GLOBAL APPEND PROPERTY LINA_COVERAGE_BINARIES "$<TARGET_FILE:lina_qml_tests>")

# The executable as shipped, started and stopped once: it must draw its window and quit
# cleanly with no audio output. This is also what measures main.cpp.
add_test(NAME lina_starts_and_quits
  COMMAND lina --no-audio --quit-after-start)
set_tests_properties(lina_starts_and_quits PROPERTIES
  ENVIRONMENT "${lina_ui_test_environment};HOME=${lina_ui_test_scratch}/starts-and-quits"
  ENVIRONMENT_MODIFICATION "${lina_ui_test_environment_modification}"
  FAIL_REGULAR_EXPRESSION "${lina_ui_test_failures}"
  TIMEOUT 120)
set_property(GLOBAL APPEND PROPERTY LINA_COVERAGE_BINARIES "$<TARGET_FILE:lina>")

# What the command line answers without bringing anything up.
add_test(NAME lina_prints_its_version COMMAND lina --version)
add_test(NAME lina_prints_help COMMAND lina --help)
add_test(NAME lina_refuses_an_unknown_option COMMAND lina --loud)
set_tests_properties(lina_prints_its_version lina_prints_help lina_refuses_an_unknown_option
  PROPERTIES
    ENVIRONMENT "${lina_ui_test_environment}"
    ENVIRONMENT_MODIFICATION "${lina_ui_test_environment_modification}"
    TIMEOUT 60)
set_tests_properties(lina_prints_its_version PROPERTIES PASS_REGULAR_EXPRESSION "LINA [0-9]+\\.[0-9]+\\.[0-9]+")
set_tests_properties(lina_prints_help PROPERTIES PASS_REGULAR_EXPRESSION "--no-audio")
set_tests_properties(lina_refuses_an_unknown_option PROPERTIES WILL_FAIL TRUE)
