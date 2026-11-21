// SPDX-License-Identifier: GPL-3.0-or-later
//
// The mixer's arithmetic (docs/design/09-mixer-routing-and-devices.md): what
// a decibel value, a pan position, mute and solo mean as gains. Plain
// functions of the document, so they are tested without an engine.
//
// Levels. A level of d decibels is a gain of 10^(d / 20); at and below
// `model::silenceDb` it is silence, a gain of exactly 0.
//
// Pan law. Constant power, with unity at the centre:
//     left  = sqrt(2) * cos((pan + 1) * pi / 4)
//     right = sqrt(2) * sin((pan + 1) * pi / 4)
// At the centre both sides are exactly 1, so a centred track at 0 dB passes
// its signal unchanged (doc 09, "Signal flow", rule 4). The sum of the
// squares is 2 everywhere, which keeps the loudness of a mono source steady
// across the range ("The mixer strip", rule 2). Panned hard to one side, that
// side is at +3.01 dB and the other is silent. Pan is a balance control: it
// scales the two sides and never moves signal from one to the other.
//
// Mute and solo ("The mixer strip", rules 3 and 4; solo on return tracks is
// LINA's reading where the doc is silent):
//   - A muted track is silent, and so are its sends.
//   - While any audio or MIDI track is soloed, the audio and MIDI tracks that
//     are not soloed are silent, sends included.
//   - Return tracks are not silenced by a solo on an audio or MIDI track.
//   - While any return track is soloed, the return tracks that are not soloed
//     are silent, and so is the direct output of every audio and MIDI track
//     that is not soloed itself. Their sends keep feeding the return tracks,
//     or a soloed return would have nothing to play.
//   - The master is never silenced.
#ifndef LINA_ENGINE_INTERNAL_MIXING_HPP
#define LINA_ENGINE_INTERNAL_MIXING_HPP

#include "lina/model/project.hpp"
#include "lina/model/track.hpp"

namespace lina::engine::internal {

/// The gain of a level in decibels; exactly 0 at and below model::silenceDb.
[[nodiscard]] double gainFromDb(double db);

struct StereoGain {
  float left{1.0F};
  float right{1.0F};

  friend bool operator==(const StereoGain &, const StereoGain &) = default;
};

/// Volume and pan of a strip as one gain per side, pan running from -1 (left)
/// to 1 (right); silence when `audible` is false.
[[nodiscard]] StereoGain stripGains(const model::MixerStrip &strip,
                                    bool audible);

/// Which kinds of track have a solo switched on.
struct SoloState {
  bool onTracks{false};
  bool onReturns{false};
};

[[nodiscard]] SoloState soloState(const model::Project &project);

/// Whether an audio or MIDI track's signal reaches its sends.
[[nodiscard]] bool trackFeedsSends(const model::MixerStrip &strip,
                                   SoloState solo);
/// Whether an audio or MIDI track's signal reaches the master directly.
[[nodiscard]] bool trackReachesMaster(const model::MixerStrip &strip,
                                      SoloState solo);
[[nodiscard]] bool returnReachesMaster(const model::MixerStrip &strip,
                                       SoloState solo);

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_MIXING_HPP
