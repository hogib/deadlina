// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/testing/edit_building.hpp"

#include "lina/engine/internal/tracktion_access.hpp"
#include "lina/engine/testing/test_plugins.hpp"

#include <stdexcept>

namespace lina::engine::testing {

namespace {

namespace te = tracktion;

/// Where a plugin goes so that it sits just before the track's own volume.
int indexBeforeVolume(te::AudioTrack &track) {
  const int index =
      track.pluginList.getPlugins().indexOf(track.getVolumePlugin());
  return index < 0 ? track.pluginList.size() : index;
}

} // namespace

void registerTestPlugins(Engine &engine) {
  auto &plugins = internal::TracktionAccess::engine(engine).getPluginManager();
  plugins.createBuiltInType<ToneInstrument>();
  plugins.createBuiltInType<DelayGainEffect>();
  plugins.createBuiltInType<MidiProbe>();
  registerTestDeviceBindings(internal::TracktionAccess::devices(engine));
}

void setTempo(Engine &engine, double bpm) {
  internal::TracktionAccess::edit(engine).tempoSequence.getTempo(0)->setBpm(
      bpm);
}

te::AudioTrack &addTrack(Engine &engine) {
  auto &edit = internal::TracktionAccess::edit(engine);
  const int count = te::getAudioTracks(edit).size();
  edit.ensureNumberOfAudioTracks(count + 1);
  auto *track = te::getAudioTracks(edit).getLast();
  if (track == nullptr) {
    throw std::runtime_error("the engine did not add an audio track");
  }
  return *track;
}

te::MidiClip &addMidiClip(te::AudioTrack &track, double startBeat,
                          double lengthBeats, std::span<const Note> notes) {
  const auto &tempo = track.edit.tempoSequence;
  const te::TimeRange range(
      tempo.toTime(te::BeatPosition::fromBeats(startBeat)),
      tempo.toTime(te::BeatPosition::fromBeats(startBeat + lengthBeats)));
  const auto clip = track.insertMIDIClip("test clip", range, nullptr);
  if (clip == nullptr) {
    throw std::runtime_error("the engine did not add a MIDI clip");
  }
  for (const auto &note : notes) {
    clip->getSequence().addNote(note.pitch,
                                te::BeatPosition::fromBeats(note.startBeat),
                                te::BeatDuration::fromBeats(note.lengthBeats),
                                note.velocity, 0, nullptr);
  }
  return *clip;
}

void addToneInstrument(te::AudioTrack &track) {
  if (track.pluginList.insertPlugin(ToneInstrument::create(), 0) == nullptr) {
    throw std::runtime_error("the engine did not create the test instrument");
  }
}

void addDelayGainEffect(te::AudioTrack &track, int delayFrames, float gain) {
  if (track.pluginList.insertPlugin(DelayGainEffect::create(delayFrames, gain),
                                    indexBeforeVolume(track)) == nullptr) {
    throw std::runtime_error("the engine did not create the test effect");
  }
}

} // namespace lina::engine::testing
