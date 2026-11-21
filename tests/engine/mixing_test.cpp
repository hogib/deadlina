// SPDX-License-Identifier: GPL-3.0-or-later
//
// The mixer's arithmetic (mixing.hpp): decibels, the pan law, mute and solo.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/mixing.hpp"

#include <cmath>
#include <numbers>

using namespace lina::engine::internal;
using namespace lina::model;

namespace {

MixerStrip strip(double volumeDb, double pan) {
  MixerStrip made;
  made.volumeDb = volumeDb;
  made.pan = pan;
  return made;
}

MixerStrip switches(bool mute, bool solo) {
  MixerStrip made;
  made.mute = mute;
  made.solo = solo;
  return made;
}

} // namespace

TEST_CASE("a level in decibels is the gain 10^(dB/20), and silence at -96",
          "[mixing]") {
  CHECK(gainFromDb(0.0) == 1.0);
  CHECK(gainFromDb(20.0) == 10.0);
  CHECK(gainFromDb(-20.0) == 0.1);
  CHECK(std::abs(gainFromDb(6.0) - 1.9952623149688795) < 1e-15);
  CHECK(std::abs(gainFromDb(-6.0) - 0.5011872336272722) < 1e-15);
  CHECK(gainFromDb(silenceDb) == 0.0);
  CHECK(gainFromDb(-200.0) == 0.0);
  // Just above silence is not silence.
  CHECK(gainFromDb(-95.9) > 1.5e-5);
}

TEST_CASE("a centred strip at 0 dB passes its signal unchanged", "[mixing]") {
  CHECK(stripGains(strip(0.0, 0.0), true) ==
        StereoGain{.left = 1.0F, .right = 1.0F});
  CHECK(stripGains(MixerStrip{}, true) ==
        StereoGain{.left = 1.0F, .right = 1.0F});
}

TEST_CASE("panning keeps the power of the two sides constant", "[mixing]") {
  const auto root2 = static_cast<float>(std::numbers::sqrt2);
  CHECK(stripGains(strip(0.0, -1.0), true) ==
        StereoGain{.left = root2, .right = 0.0F});
  CHECK(stripGains(strip(0.0, 1.0), true) ==
        StereoGain{.left = 0.0F, .right = root2});
  // Half way to the right: the angle is 3/8 of pi.
  const auto half = stripGains(strip(0.0, 0.5), true);
  const float angle = 3.0F * std::numbers::pi_v<float> / 8.0F;
  CHECK(std::abs(half.left - (root2 * std::cos(angle))) < 1e-6F);
  CHECK(std::abs(half.right - (root2 * std::sin(angle))) < 1e-6F);
  for (int step = -100; step <= 100; ++step) {
    CAPTURE(step);
    const auto gain = stripGains(strip(0.0, step / 100.0), true);
    // Tolerance: 1e-6 on a power of 2.
    CHECK(std::abs((gain.left * gain.left) + (gain.right * gain.right) - 2.0F) <
          1e-6F);
    // Moving right never raises the left side or lowers the right.
    const auto next = stripGains(strip(0.0, (step + 1) / 100.0), true);
    CHECK(next.left <= gain.left);
    CHECK(next.right >= gain.right);
  }
}

TEST_CASE("volume scales both sides and silence is exact", "[mixing]") {
  const auto louder = stripGains(strip(6.0, 0.0), true);
  CHECK(louder.left == static_cast<float>(gainFromDb(6.0)));
  CHECK(louder.right == louder.left);
  const auto panned = stripGains(strip(-12.0, -1.0), true);
  CHECK(panned.left ==
        static_cast<float>(gainFromDb(-12.0) * std::numbers::sqrt2));
  CHECK(panned.right == 0.0F);
  // The same in numbers worked out by hand: 10^(6/20), 10^(-12/20) * sqrt(2),
  // and sqrt(2) * cos and sin of 3/8 pi for a pan of one half.
  CHECK(std::abs(louder.left - 1.9952623F) < 2e-7F);
  CHECK(std::abs(panned.left - 0.3552344F) < 1e-7F);
  const auto half = stripGains(strip(0.0, 0.5), true);
  CHECK(std::abs(half.left - 0.5411961F) < 1e-7F);
  CHECK(std::abs(half.right - 1.3065630F) < 2e-7F);
  CHECK(stripGains(strip(silenceDb, 0.3), true) ==
        StereoGain{.left = 0.0F, .right = 0.0F});
  CHECK(stripGains(strip(6.0, 0.3), false) ==
        StereoGain{.left = 0.0F, .right = 0.0F});
}

TEST_CASE("solo is found per kind of track", "[mixing]") {
  Project project = makeEmptyProject();
  CHECK(!soloState(project).onTracks);
  CHECK(!soloState(project).onReturns);
  project.tracks.resize(2);
  project.returns.resize(2);
  project.tracks.at(1).mixer.solo = true;
  CHECK(soloState(project).onTracks);
  CHECK(!soloState(project).onReturns);
  project.tracks.at(1).mixer.solo = false;
  project.returns.at(0).mixer.solo = true;
  CHECK(!soloState(project).onTracks);
  CHECK(soloState(project).onReturns);
}

TEST_CASE("mute and solo decide what reaches the master", "[mixing]") {
  const SoloState none{};
  const SoloState tracks{.onTracks = true, .onReturns = false};
  const SoloState returns{.onTracks = false, .onReturns = true};
  const SoloState both{.onTracks = true, .onReturns = true};
  const MixerStrip plain = switches(false, false);
  const MixerStrip muted = switches(true, false);
  const MixerStrip soloed = switches(false, true);
  const MixerStrip mutedAndSoloed = switches(true, true);

  SECTION("without a solo only mute counts") {
    CHECK(trackReachesMaster(plain, none));
    CHECK(trackFeedsSends(plain, none));
    CHECK(returnReachesMaster(plain, none));
    CHECK(!trackReachesMaster(muted, none));
    CHECK(!trackFeedsSends(muted, none));
    CHECK(!returnReachesMaster(muted, none));
  }
  SECTION("a soloed track silences the others, sends included, and leaves "
          "the return tracks alone") {
    CHECK(trackReachesMaster(soloed, tracks));
    CHECK(trackFeedsSends(soloed, tracks));
    CHECK(!trackReachesMaster(plain, tracks));
    CHECK(!trackFeedsSends(plain, tracks));
    CHECK(returnReachesMaster(plain, tracks));
    CHECK(!returnReachesMaster(muted, tracks));
  }
  SECTION("a soloed return silences the other returns and the direct output "
          "of the tracks, whose sends keep feeding it") {
    CHECK(returnReachesMaster(soloed, returns));
    CHECK(!returnReachesMaster(plain, returns));
    CHECK(!trackReachesMaster(plain, returns));
    CHECK(trackFeedsSends(plain, returns));
    CHECK(!trackFeedsSends(muted, returns));
  }
  SECTION("with solos on both kinds each kind follows its own") {
    CHECK(trackReachesMaster(soloed, both));
    CHECK(trackFeedsSends(soloed, both));
    CHECK(!trackReachesMaster(plain, both));
    CHECK(!trackFeedsSends(plain, both));
    CHECK(returnReachesMaster(soloed, both));
    CHECK(!returnReachesMaster(plain, both));
  }
  SECTION("mute wins over solo") {
    CHECK(!trackReachesMaster(mutedAndSoloed, tracks));
    CHECK(!trackFeedsSends(mutedAndSoloed, tracks));
    CHECK(!returnReachesMaster(mutedAndSoloed, returns));
  }
}
