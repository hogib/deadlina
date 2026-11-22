// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_ENGINE_TESTING_TEST_PLUGINS_HPP
#define LINA_ENGINE_TESTING_TEST_PLUGINS_HPP

#include "lina/engine/internal/device_registry.hpp"
#include "lina/model/track.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lina::engine::testing {

/// Test instrument whose output is known in closed form.
///
/// A note-on of MIDI note p and velocity v that lands on sample frame s makes
/// the instrument write, into every output channel and for every frame n >= s
/// until the matching note-off,
///
///     (v / 127) * cos(2 * pi * f(p) * (n - s) / sampleRate),
///     f(p) = 440 * 2^((p - 69) / 12).
///
/// The phase is computed from the integer age n - s in double precision, so
/// the output does not depend on how the stream is cut into blocks. Notes sum.
/// There is no envelope: the first frame of a note is at full level and the
/// frame of the note-off is silent. An event between two frames takes effect
/// on the nearer one.
class ToneInstrument final : public tracktion::Plugin {
public:
  static constexpr const char *xmlTypeName = "linaTestTone";
  static constexpr int voiceCount = 16;

  explicit ToneInstrument(const tracktion::PluginCreationInfo &info);
  ~ToneInstrument() override;

  ToneInstrument(const ToneInstrument &) = delete;
  ToneInstrument &operator=(const ToneInstrument &) = delete;
  ToneInstrument(ToneInstrument &&) = delete;
  ToneInstrument &operator=(ToneInstrument &&) = delete;

  static const char *getPluginName() { return "LINA test tone"; }
  static juce::ValueTree create();

  juce::String getName() const override { return getPluginName(); }
  juce::String getPluginType() override { return xmlTypeName; }
  juce::String getSelectableDescription() override { return getName(); }

  bool takesMidiInput() override { return true; }
  bool isSynth() override { return true; }
  bool producesAudioWhenNoAudioInput() override { return true; }
  int getNumOutputChannelsGivenInputs(
      [[maybe_unused]] int inputChannelCount) override {
    return 2;
  }

  void initialise(const tracktion::PluginInitialisationInfo &info) override;
  void deinitialise() override;
  void reset() override;
  void applyToBuffer(const tracktion::PluginRenderContext &context)
      [[clang::nonblocking]] override;

private:
  struct Voice {
    bool active = false;
    int note = 0;
    double level = 0.0;
    double frequency = 0.0;
    std::int64_t age = 0;
  };

  void handle(const juce::MidiMessage &message) [[clang::nonblocking]];
  void render(const tracktion::PluginRenderContext &context, int from, int to)
      [[clang::nonblocking]];
  void silence() [[clang::nonblocking]];

  std::array<Voice, voiceCount> voices_{};
  double sampleRate_ = 0.0;
};

/// Test effect: every channel comes out delayed by a whole number of sample
/// frames and multiplied by a gain, y[n] = gain * x[n - delay], with silence
/// before the first delayed frame. It reports no latency, so the engine does
/// not compensate for the delay and the shift is audible.
///
/// Both values are read from the plugin's state when the plugin is made
/// ready to play; set them before playing or rendering. The gain can also be
/// set while the plugin plays (setGain), and takes effect with the next
/// block; the delay cannot.
class DelayGainEffect final : public tracktion::Plugin {
public:
  static constexpr const char *xmlTypeName = "linaTestDelayGain";
  static constexpr std::size_t maxChannels = 2;

  explicit DelayGainEffect(const tracktion::PluginCreationInfo &info);
  ~DelayGainEffect() override;

  DelayGainEffect(const DelayGainEffect &) = delete;
  DelayGainEffect &operator=(const DelayGainEffect &) = delete;
  DelayGainEffect(DelayGainEffect &&) = delete;
  DelayGainEffect &operator=(DelayGainEffect &&) = delete;

  static const char *getPluginName() { return "LINA test delay and gain"; }
  static juce::ValueTree create(int delayFrames, float gain);

  /// Writes the gain to the plugin's state and to the running plugin.
  /// Message thread.
  void setGain(float gain);

  juce::String getName() const override { return getPluginName(); }
  juce::String getPluginType() override { return xmlTypeName; }
  juce::String getSelectableDescription() override { return getName(); }

  void initialise(const tracktion::PluginInitialisationInfo &info) override;
  void deinitialise() override;
  void reset() override;
  void applyToBuffer(const tracktion::PluginRenderContext &context)
      [[clang::nonblocking]] override;

private:
  /// The last `delay` input frames of each channel, used as a ring.
  std::array<std::vector<float>, maxChannels> history_;
  std::size_t position_ = 0;
  std::atomic<float> gain_{1.0F};
};

/// Test plugin that writes down the note messages that pass through it, with
/// the frame of the arrangement each one falls on, and changes nothing: audio
/// and MIDI go on as they came. Placed behind the test instrument, which
/// hands on the MIDI it is given, it tells what the instrument was asked to
/// play.
///
/// It keeps up to `capacity` messages; take() hands them over and starts
/// afresh. Recording runs on the audio thread and take() on the message
/// thread, between blocks; the engine of a test runs its blocks on the
/// message thread, so the two never overlap.
class MidiProbe final : public tracktion::Plugin {
public:
  static constexpr const char *xmlTypeName = "linaTestMidiProbe";
  static constexpr std::size_t capacity = 262'144;

  /// One message: a note-on (velocity 1 to 127), a note-off (velocity 0), or
  /// a block that told the instrument to end every note.
  struct Note {
    std::int64_t frame = 0;
    int pitch = 0;
    int velocity = 0;
    bool allNotesOff = false;
    bool playing = false;
  };

  explicit MidiProbe(const tracktion::PluginCreationInfo &info);
  ~MidiProbe() override;

  MidiProbe(const MidiProbe &) = delete;
  MidiProbe &operator=(const MidiProbe &) = delete;
  MidiProbe(MidiProbe &&) = delete;
  MidiProbe &operator=(MidiProbe &&) = delete;

  static const char *getPluginName() { return "LINA test MIDI probe"; }
  static juce::ValueTree create();

  /// What passed since the last call, in order. Message thread.
  std::vector<Note> take();
  /// True when more passed than the probe keeps.
  [[nodiscard]] bool overflowed() const { return overflowed_.load(); }
  /// The frame the last block began on.
  [[nodiscard]] std::int64_t lastBlockStart() const {
    return lastBlockStart_.load();
  }

  juce::String getName() const override { return getPluginName(); }
  juce::String getPluginType() override { return xmlTypeName; }
  juce::String getSelectableDescription() override { return getName(); }
  bool takesMidiInput() override { return true; }

  void initialise(const tracktion::PluginInitialisationInfo &info) override;
  void deinitialise() override;
  void applyToBuffer(const tracktion::PluginRenderContext &context)
      [[clang::nonblocking]] override;

private:
  void record(const Note &note) [[clang::nonblocking]];

  std::vector<Note> notes_;
  std::atomic<std::size_t> count_{0};
  std::atomic<bool> overflowed_{false};
  std::atomic<std::int64_t> lastBlockStart_{0};
  std::atomic<double> sampleRate_{0.0};
};

/// The format under which the test plugins are known to devices of a
/// document, with the instrument's and the effect's type names as
/// identifiers.
inline constexpr const char *testPluginFormat = "lina-test";

/// The longest delay a device can ask of the test effect, in frames.
inline constexpr int maxDeviceDelayFrames = 65'536;

/// Devices that stand for the test plugins. The effect takes its values from
/// two parameters, normalised as the document wants them:
///   "delay": delay in frames = round(value * maxDeviceDelayFrames)
///   "gain":  gain = 4 * value - 2
/// A parameter that the device does not list is at its default: no delay,
/// unity gain.
model::Device toneDevice();
model::Device delayGainDevice(int delayFrames, float gain);
model::Device midiProbeDevice();

/// Makes the test plugins available to devices of a document.
void registerTestDeviceBindings(internal::DeviceRegistry &registry);

} // namespace lina::engine::testing

#endif // LINA_ENGINE_TESTING_TEST_PLUGINS_HPP
