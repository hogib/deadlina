// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/testing/test_environment.hpp"

#include <cstdio>
#include <cstdlib>
#include <print>
#include <stdexcept>

#ifdef __has_feature
#if __has_feature(thread_sanitizer)
// ThreadSanitizer reads its suppressions from this function when a test
// executable defines it. One race in third-party code is suppressed, for
// engine tests only:
//
//   JUCE 8.0.6, juce_AudioFormatReader.cpp:440. Every engine has a thread
//   that touches the pages of the audio files it has mapped, and
//   MemoryMappedAudioFormatReader::touchSample adds each byte it reads to
//   one static int, `memoryReadDummyVariable`, whose only purpose is to
//   keep the compiler from dropping the read. Two engines in one process
//   that both play audio files, which only tests have, write it from two
//   threads. The value is never used.
//
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" const char *__tsan_default_suppressions() {
  return "race:juce::MemoryMappedAudioFormatReader::touchSample\n";
}
#endif
#endif

namespace lina::engine::testing {

AssertionCounter::AssertionCounter() { juce::Logger::setCurrentLogger(this); }

AssertionCounter::~AssertionCounter() {
  juce::Logger::setCurrentLogger(nullptr);
}

void AssertionCounter::logMessage(const juce::String &message) {
  if (message.startsWith("JUCE Assertion failure")) {
    ++count_;
  }
  std::println(stderr, "{}", message.toRawUTF8());
}

std::filesystem::path enterScratchFolder(std::string_view name) {
  const auto folder = std::filesystem::path(LINA_TEST_SCRATCH_DIR) / name;
  std::filesystem::remove_all(folder);
  const auto temporary = folder / "tmp";
  std::filesystem::create_directories(temporary);
  // NOLINTBEGIN(concurrency-mt-unsafe)
  if (::setenv("HOME", folder.c_str(), 1) != 0 ||
      ::setenv("TMPDIR", temporary.c_str(), 1) != 0 ||
      ::unsetenv("DISPLAY") != 0) {
    throw std::runtime_error("could not set up the test environment");
  }
  // NOLINTEND(concurrency-mt-unsafe)
  return folder;
}

} // namespace lina::engine::testing
