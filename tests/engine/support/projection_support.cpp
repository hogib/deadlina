// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/testing/projection_support.hpp"

#include "lina/engine/internal/tracktion_access.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace lina::engine::testing {

namespace {

namespace te = tracktion;

using internal::Projection;

/// How many events of a MIDI clip are written out; the rest is covered by a
/// count and a checksum.
constexpr int listedEvents = 512;

const char *kindName(model::TrackKind kind) {
  switch (kind) {
  case model::TrackKind::Audio:
    return "audio";
  case model::TrackKind::Midi:
    return "midi";
  case model::TrackKind::Return:
    return "return";
  case model::TrackKind::Master:
    break;
  }
  return "master";
}

/// Writes the description and keeps count of what it has seen.
class Describer {
public:
  explicit Describer(Engine &engine)
      : edit_(&internal::TracktionAccess::edit(engine)),
        projection_(&internal::TracktionAccess::projection(engine)) {}

  std::string run() {
    const auto time = projection_->timeMap();
    const auto &tempo = edit_->tempoSequence;
    line("tempo {} bpm, {}/{}, {} tempos, {} signatures", tempo.getBpmAt({}),
         tempo.getTimeSig(0)->numerator.get(),
         tempo.getTimeSig(0)->denominator.get(), tempo.getNumTempos(),
         tempo.getNumTimeSigs());
    line("sample rate {}", time.sampleRate());
    line("engine master: position {} pan {} law {} fades {}/{}, click {}, "
         "loop {}",
         edit_->getMasterSliderPosParameter()->getCurrentValue(),
         edit_->getMasterPanParameter()->getCurrentValue(),
         static_cast<int>(edit_->getMasterVolumePlugin()->getPanLaw()),
         edit_->masterFadeIn.get().inSeconds(),
         edit_->masterFadeOut.get().inSeconds(), edit_->clickTrackEnabled.get(),
         edit_->getTransport().looping.get());
    line("undo history: {}",
         edit_->getUndoManager().canUndo() ? "kept" : "none");
    line("master");
    for (auto *plugin : edit_->getMasterPluginList().getPlugins()) {
      if (plugin == projection_->masterStrip().get()) {
        line("  strip {}", gains(*projection_->masterStrip()));
      } else {
        this->plugin(*plugin, nullptr);
      }
    }
    for (auto *track : te::getAudioTracks(*edit_)) {
      this->track(*track);
    }
    leftovers();
    for (const auto &[path, file] : projection_->files()) {
      line("file {}: rate {} playable {} problem {}", path, file.sampleRate,
           file.playable(),
           file.problem.has_value() ? static_cast<int>(*file.problem) : -1);
    }
    for (const auto &report : projection_->mediaProblems()) {
      line("media {} problem {} file rate {}", report.media.value,
           static_cast<int>(report.problem), report.fileSampleRate);
    }
    for (const auto clip : projection_->truncatedClips()) {
      line("clip {} holds more notes than are played", clip.value);
    }
    return std::move(text_);
  }

private:
  template <class... Args>
  void line(std::format_string<Args...> format, Args &&...args) {
    text_ += std::format(format, std::forward<Args>(args)...);
    text_ += '\n';
  }

  static std::string gains(const internal::StripGainPlugin &plugin) {
    const auto gain = plugin.gains();
    return std::format("left {} right {}", gain.left, gain.right);
  }

  void track(te::AudioTrack &track) {
    const auto &tracks = projection_->tracks();
    const auto known = std::ranges::find_if(tracks, [&](const auto &item) {
      return item.second.track.get() == &track;
    });
    if (known == tracks.end()) {
      line("track not in the projection");
      return;
    }
    seenTracks_.insert(known->first);
    const Projection::TrackEntry &entry = known->second;
    line("track {} {}", known->first.value, kindName(entry.kind));
    line("  engine track: mute {} solo {} isolate {}", track.isMuted(false),
         track.isSolo(false), track.isSoloIsolate(false));
    for (auto *plugin : track.pluginList.getPlugins()) {
      this->plugin(*plugin, &entry);
    }
    std::vector<te::Clip *> clips;
    for (auto *clip : track.getClips()) {
      clips.push_back(clip);
    }
    std::ranges::sort(clips, [&](te::Clip *a, te::Clip *b) {
      return std::tuple(a->getPosition().getStart(), owner(*a)) <
             std::tuple(b->getPosition().getStart(), owner(*b));
    });
    for (auto *clip : clips) {
      this->clip(*clip, known->first);
    }
  }

  void plugin(te::Plugin &plugin, const Projection::TrackEntry *entry) {
    if (entry != nullptr && &plugin == entry->strip.get()) {
      line("  strip {}", gains(*entry->strip));
      return;
    }
    if (entry != nullptr && &plugin == entry->guard.get()) {
      line("  note guard");
      return;
    }
    if (entry != nullptr && &plugin == entry->output.get()) {
      line("  output {}", gains(*entry->output));
      return;
    }
    if (entry != nullptr && &plugin == entry->input.get()) {
      line("  input from the sends, enabled {}", plugin.isEnabled());
      return;
    }
    if (entry != nullptr) {
      const auto send =
          std::ranges::find_if(entry->sends, [&](const Projection::Send &item) {
            return item.plugin.get() == &plugin;
          });
      if (send != entry->sends.end()) {
        line("  send to {}: position {}, reaches {}", send->target.value,
             send->plugin->gain->getCurrentValue(),
             busTarget(send->plugin->getBusNumber()));
        return;
      }
    }
    const auto &devices = projection_->devices();
    const auto device = std::ranges::find_if(devices, [&](const auto &item) {
      return item.second.plugin.get() == &plugin;
    });
    if (device == devices.end()) {
      line("  plugin not in the projection: {}",
           plugin.getPluginType().toStdString());
      return;
    }
    seenDevices_.insert(device->first);
    std::string properties;
    std::map<std::string, std::string> sorted;
    for (int index = 0; index < plugin.state.getNumProperties(); ++index) {
      const auto name = plugin.state.getPropertyName(index);
      if (name != te::IDs::id) {
        sorted.emplace(name.toString().toStdString(),
                       plugin.state.getProperty(name).toString().toStdString());
      }
    }
    for (const auto &[name, value] : sorted) {
      properties += std::format(" {}={}", name, value);
    }
    line("  device {} enabled {}{}{}", device->first.value, plugin.isEnabled(),
         device->second.binding == nullptr ? " placeholder" : "", properties);
  }

  /// The return track whose input listens on a bus, as text.
  [[nodiscard]] std::string busTarget(int bus) const {
    for (const auto &[id, entry] : projection_->tracks()) {
      if (entry.input != nullptr && entry.bus == bus &&
          dynamic_cast<te::AuxReturnPlugin &>(*entry.input).busNumber.get() ==
              bus) {
        return std::to_string(id.value);
      }
    }
    return "nothing";
  }

  /// The clip of the document an engine clip belongs to, or 0.
  [[nodiscard]] std::uint64_t owner(const te::Clip &clip) const {
    for (const auto &[id, entry] : projection_->clips()) {
      if (std::ranges::any_of(entry.parts, [&](const te::Clip::Ptr &part) {
            return part.get() == &clip;
          })) {
        return id.value;
      }
    }
    return 0;
  }

  void clip(te::Clip &clip, model::TrackId track) {
    const model::ClipId id{owner(clip)};
    const auto &clips = projection_->clips();
    const auto known = clips.find(id);
    if (known == clips.end() || known->second.track != track) {
      line("  clip not in the projection, or on another track");
      return;
    }
    ++seenParts_[id];
    const auto position = clip.getPosition();
    const auto time = projection_->timeMap();
    const std::string where =
        std::format("frames {:.6f} to {:.6f} offset {:.9f}",
                    position.getStart().inSeconds() * time.sampleRate(),
                    position.getEnd().inSeconds() * time.sampleRate(),
                    position.getOffset().inSeconds());
    if (const auto *midi = dynamic_cast<te::MidiClip *>(&clip)) {
      line("  clip {} midi {} origin {} seconds {} channel {} looping {} mute "
           "{} volume {}",
           id.value, where, known->second.originFrame, midi->canUseProxy(),
           midi->getMidiChannel().getChannelNumber(), midi->isLooping(),
           midi->isMuted(), midi->getVolumeDb());
      events(known->second);
      return;
    }
    const auto &audio = dynamic_cast<te::WaveAudioClip &>(clip);
    line("  clip {} audio {} file {} converter {} gain {} loop {:.9f}+{:.9f} "
         "looping {} proxy {} tempo {} pitch {} stretch {} speed {} reversed "
         "{} fades {}/{} crossfade {} pan {} mute {}",
         id.value, where,
         audio.getOriginalFile().getFullPathName().toStdString(),
         static_cast<int>(audio.getResamplingQuality()), audio.getGainDB(),
         audio.getLoopStart().inSeconds(), audio.getLoopLength().inSeconds(),
         audio.isLooping(), audio.canUseProxy(), audio.getAutoTempo(),
         audio.getAutoPitch(), static_cast<int>(audio.getTimeStretchMode()),
         audio.getSpeedRatio(), audio.getIsReversed(),
         audio.getFadeIn().inSeconds(), audio.getFadeOut().inSeconds(),
         audio.getAutoCrossfade(), audio.getPan(), audio.isMuted());
  }

  /// The events the projection hands the engine for a MIDI clip. Asking
  /// for them changes nothing.
  void events(const Projection::ClipEntry &entry) {
    const auto events = projection_->eventsOf(entry);
    std::uint64_t checksum = 14'695'981'039'346'656'037ULL;
    const auto mix = [&](std::uint64_t value) {
      constexpr std::uint64_t prime = 1'099'511'628'211ULL;
      checksum = (checksum ^ value) * prime;
    };
    int index = 0;
    for (const internal::NoteEvent &event : events->events) {
      mix(static_cast<std::uint64_t>(event.frame));
      mix(event.pitch);
      mix(event.velocity);
      if (index++ < listedEvents) {
        line("    {} {} {} {}", event.frame, event.isNoteOn() ? "on" : "off",
             event.pitch, event.velocity);
      }
    }
    line("    {} events from {} to {}, checksum {}{}", events->events.size(),
         events->startFrame, events->endFrame, checksum,
         events->truncated ? ", cut short" : "");
  }

  /// Entries of the projection whose engine objects the walk did not meet.
  void leftovers() {
    for (const auto &[id, entry] : projection_->tracks()) {
      if (!seenTracks_.contains(id)) {
        line("track {} of the projection is not in the edit", id.value);
      }
    }
    for (const auto &[id, entry] : projection_->devices()) {
      if (!seenDevices_.contains(id)) {
        line("device {} of the projection is not in the edit", id.value);
      }
    }
    for (const auto &[id, entry] : projection_->clips()) {
      const auto seen = seenParts_.find(id);
      const std::size_t count = seen == seenParts_.end() ? 0 : seen->second;
      if (count != entry.parts.size()) {
        line("clip {} of the projection has {} engine clips, {} in the edit",
             id.value, entry.parts.size(), count);
      } else if (entry.parts.empty()) {
        line("clip {} lies behind the horizon or plays nothing", id.value);
      }
    }
  }

  te::Edit *edit_;
  const Projection *projection_;
  std::string text_;
  std::set<model::TrackId> seenTracks_;
  std::set<model::DeviceId> seenDevices_;
  std::map<model::ClipId, std::size_t> seenParts_;
};

} // namespace

std::string describeEdit(Engine &engine) { return Describer(engine).run(); }

void writeWav(const std::filesystem::path &file, const AudioBuffer &audio) {
  std::filesystem::create_directories(file.parent_path());
  std::filesystem::remove(file);
  auto stream =
      std::make_unique<juce::FileOutputStream>(juce::File(file.string()));
  juce::WavAudioFormat wav;
  constexpr int bitsPerSample = 32;
  const std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
      stream.get(), audio.sampleRate,
      static_cast<unsigned int>(audio.channels.size()), bitsPerSample, {}, 0));
  if (writer == nullptr) {
    throw std::runtime_error("cannot write " + file.string());
  }
  // The writer has taken the stream over, which is JUCE's ownership idiom.
  std::ignore = stream.release();
  std::vector<const float *> pointers;
  pointers.reserve(audio.channels.size());
  for (const auto &channel : audio.channels) {
    pointers.push_back(channel.data());
  }
  if (!writer->writeFromFloatArrays(pointers.data(),
                                    static_cast<int>(pointers.size()),
                                    static_cast<int>(audio.frameCount()))) {
    throw std::runtime_error("cannot write " + file.string());
  }
}

AudioBuffer readWav(const std::filesystem::path &file) {
  juce::WavAudioFormat wav;
  auto stream =
      std::make_unique<juce::FileInputStream>(juce::File(file.string()));
  if (!stream->openedOk()) {
    throw std::runtime_error("cannot open " + file.string());
  }
  // The reader takes the stream over, also when it cannot read it.
  const std::unique_ptr<juce::AudioFormatReader> reader(
      wav.createReaderFor(stream.release(), true));
  if (reader == nullptr) {
    throw std::runtime_error("not a WAV file: " + file.string());
  }
  AudioBuffer audio;
  audio.sampleRate = reader->sampleRate;
  const auto frameCount = static_cast<int>(reader->lengthInSamples);
  juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                  frameCount);
  reader->read(&buffer, 0, frameCount, 0, true, true);
  for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
    const std::span samples(buffer.getReadPointer(channel),
                            static_cast<std::size_t>(frameCount));
    audio.channels.emplace_back(samples.begin(), samples.end());
  }
  return audio;
}

} // namespace lina::engine::testing
