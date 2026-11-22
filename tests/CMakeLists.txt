# SPDX-License-Identifier: GPL-3.0-or-later
#
# Test tiers (docs/design/14-build-test-and-ci.md). Each tier is added by the work package
# that brings its library.
add_subdirectory(model)
add_subdirectory(format)
if(LINA_ENABLE_ENGINE)
  add_subdirectory(engine)
  add_subdirectory(bridge)
  add_subdirectory(ui)
endif()
