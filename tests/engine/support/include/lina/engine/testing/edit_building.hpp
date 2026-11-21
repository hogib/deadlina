// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building the edit of an Engine by hand, with Tracktion calls, for tests of
// the engine tier that run before the projection from the document exists.
#ifndef LINA_ENGINE_TESTING_EDIT_BUILDING_HPP
#define LINA_ENGINE_TESTING_EDIT_BUILDING_HPP

#include "lina/engine/engine.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <span>

namespace lina::engine::testing {

/// A note in a MIDI clip. Beats are quarter notes from the start of the clip.
struct Note {
  int pitch = 60;
  double startBeat = 0.0;
  double lengthBeats = 1.0;
  int velocity = 127;
};

/// Makes the test instrument and the test effect available to the engine's
/// edit, and to devices of a document that name them (test_plugins.hpp).
/// Call once per Engine, before adding either to a track and before setting
/// a project.
void registerTestPlugins(Engine &engine);

/// Sets the single tempo of the engine's edit, in quarter notes per minute.
void setTempo(Engine &engine, double bpm);

/// Appends an audio track to the engine's edit. A track starts with the
/// engine's volume-and-pan and level-meter plugins, at unity gain and centre
/// pan.
tracktion::AudioTrack &addTrack(Engine &engine);

/// Adds a MIDI clip that starts `startBeat` quarter notes into the edit.
tracktion::MidiClip &addMidiClip(tracktion::AudioTrack &track, double startBeat,
                                 double lengthBeats,
                                 std::span<const Note> notes);

/// Puts the test instrument first in the track's plugin chain.
void addToneInstrument(tracktion::AudioTrack &track);

/// Puts the test effect after the plugins added so far and before the
/// track's volume and level meter.
void addDelayGainEffect(tracktion::AudioTrack &track, int delayFrames,
                        float gain);

} // namespace lina::engine::testing

#endif // LINA_ENGINE_TESTING_EDIT_BUILDING_HPP
