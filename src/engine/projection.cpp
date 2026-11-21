// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/projection.hpp"

#include "lina/engine/internal/mixing.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace lina::engine::internal {

namespace {

namespace te = tracktion;

using model::ChangeKind;
using model::ObjectKind;
using model::Property;
using model::PropertySet;

/// Event times are handed to the engine this far into their sample frame:
/// note-offs an eighth of a frame, note-ons three eighths.
///
/// The engine places an event by subtracting two times in seconds, which
/// loses up to a few millionths of a frame at the end of the timeline (one
/// unit in the last place of 171,000 seconds is 6e-6 frames at 192 kHz).
/// Being well inside the frame keeps that from moving an event across a
/// frame or block boundary, whether the receiver rounds or truncates. Being
/// a quarter of a frame apart keeps a note-off before a note-on of the same
/// frame when two clips meet: their events come from two nodes of the
/// engine and are merged by time, and with equal times the noise would
/// decide the order and could end the new note as it starts.
constexpr double noteOffOffsetFrames = 0.125;
constexpr double noteOnOffsetFrames = 0.375;

/// The engine starts notes that are sounding at a block's start again when
/// that block begins within 0.00001 seconds of a MIDI clip's start, and it
/// stops handing out a clip's events with the block that reaches the clip's
/// end. The engine clip of a MIDI clip therefore begins and ends this much
/// outside the range its events lie in.
constexpr double midiGuardSeconds = 0.0002;

/// The level the engine treats as silence.
constexpr float engineSilenceDb = -100.0F;

/// Notes carry no channel in the document
/// (docs/design/08-midi-clips-and-note-editing.md); the engine gets them all
/// on this one.
constexpr int midiChannel = 1;

std::int64_t midiGuardFrames(double sampleRate) {
  return static_cast<std::int64_t>(std::ceil(midiGuardSeconds * sampleRate)) +
         2;
}

float engineDb(double db) {
  return db <= model::silenceDb ? engineSilenceDb : static_cast<float>(db);
}

/// True when a change says that one of the properties changed.
bool touches(const model::Change &change,
             std::initializer_list<Property> properties) {
  return change.kind == ChangeKind::Changed &&
         std::ranges::any_of(properties, [&](Property property) {
           return change.properties.contains(property);
         });
}

/// The engine answers with null when it will not make what it was asked
/// for: a plugin of a type nobody registered, or one object more than its
/// limits allow. That must not pass for success.
void requireMade(bool made, const char *what) {
  if (!made) {
    throw std::runtime_error(std::string("the engine did not create ") + what);
  }
}

te::Plugin::Ptr insertPlugin(te::PluginList &list,
                             const juce::ValueTree &state) {
  auto plugin = list.insertPlugin(state, -1);
  requireMade(plugin != nullptr, "a plugin");
  return plugin;
}

/// The media reference of an audio clip. A document always has it; a
/// project that was never checked may not.
const model::MediaReference &mediaOf(const model::Project &project,
                                     model::MediaId media) {
  const model::MediaReference *reference = model::findMedia(project, media);
  if (reference == nullptr) {
    throw std::invalid_argument(
        "the project has an audio clip without its media reference");
  }
  return *reference;
}

/// Puts the plugins of a list into the given order. Plugins of the list that
/// are not named end up behind them.
void orderPlugins(te::PluginList &list,
                  const std::vector<te::Plugin *> &wanted) {
  for (std::size_t index = 0; index < wanted.size(); ++index) {
    const auto position = static_cast<int>(index);
    if (list.getPlugins().getObjectPointer(position) != wanted.at(index)) {
      list.insertPlugin(te::Plugin::Ptr(wanted.at(index)), position, nullptr);
      requireMade(list.getPlugins().getObjectPointer(position) ==
                      wanted.at(index),
                  "a place in a plugin chain");
    }
  }
}

template <class Plugin>
juce::ReferenceCountedObjectPtr<Plugin> insertOwn(te::PluginList &list,
                                                  const char *what) {
  const auto plugin = insertPlugin(list, Plugin::create());
  juce::ReferenceCountedObjectPtr<Plugin> own(
      dynamic_cast<Plugin *>(plugin.get()));
  requireMade(own != nullptr, what);
  return own;
}

template <class Each>
void forEachArrangementClip(const model::Project &project, const Each &each) {
  for (const model::Track &track : project.tracks) {
    for (const model::Clip &clip : track.arrangement) {
      each(track, clip);
    }
  }
}

template <class Each>
void forEachTrack(const model::Project &project, const Each &each) {
  for (const model::Track &track : project.tracks) {
    each(track);
  }
  for (const model::Track &track : project.returns) {
    each(track);
  }
}

} // namespace

/// What a change set asks for, sorted into the work it leads to.
struct Projection::Pending {
  bool timing{false};
  bool trackList{false};
  bool mixer{false};
  bool allChains{false};
  std::set<model::TrackId> chains;
  std::set<model::DeviceId> devices;
  std::vector<model::DeviceId> removedDevices;
  std::set<model::ClipId> clips;
  std::vector<model::ClipId> removedClips;
  std::set<model::MediaId> media;

  void see(const model::Change &change);
  void seeProject(const model::Change &change);
  void seeTrack(const model::Change &change);
  void seeDevice(const model::Change &change);
  void seeClip(const model::Change &change);
};

void Projection::Pending::see(const model::Change &change) {
  // Scenes, slots and envelopes are not projected.
  const ObjectKind kind = change.object.kind;
  if (kind == ObjectKind::Project) {
    seeProject(change);
  } else if (kind == ObjectKind::Media) {
    if (touches(change, {Property::Path})) {
      media.insert(model::MediaId{change.object.id});
    }
  } else if (kind == ObjectKind::Track) {
    seeTrack(change);
  } else if (kind == ObjectKind::Device) {
    seeDevice(change);
  } else if (kind == ObjectKind::Clip) {
    seeClip(change);
  } else if (kind == ObjectKind::Note) {
    clips.insert(model::ClipId{change.parent.id});
  }
}

void Projection::Pending::seeProject(const model::Change &change) {
  // The project has one entry at most.
  timing = touches(change, {Property::Tempo, Property::TimeSignature});
  if (touches(change, {Property::TrackOrder, Property::ReturnOrder})) {
    trackList = true;
    allChains = true;
  }
}

void Projection::Pending::seeTrack(const model::Change &change) {
  const bool restored = change.kind == ChangeKind::Changed &&
                        change.properties == PropertySet::all();
  if (change.kind != ChangeKind::Changed || restored) {
    trackList = true;
    allChains = true;
    mixer = true;
    return;
  }
  if (touches(change, {Property::DeviceOrder})) {
    chains.insert(model::TrackId{change.object.id});
  }
  mixer = mixer || touches(change, {
                                       Property::Volume,
                                       Property::Pan,
                                       Property::Mute,
                                       Property::Solo,
                                       Property::Sends,
                                   });
}

void Projection::Pending::seeDevice(const model::Change &change) {
  const model::DeviceId device{change.object.id};
  if (change.kind == ChangeKind::Removed) {
    removedDevices.push_back(device);
    return;
  }
  // A device that moved is also reported on both of its tracks, as a change
  // of their device order.
  if (change.kind == ChangeKind::Added ||
      change.properties == PropertySet::all()) {
    chains.insert(model::TrackId{change.parent.id});
  }
  if (change.kind == ChangeKind::Added ||
      touches(change,
              {Property::Bypassed, Property::State, Property::Parameters})) {
    devices.insert(device);
  }
}

void Projection::Pending::seeClip(const model::Change &change) {
  const model::ClipId clip{change.object.id};
  if (change.kind == ChangeKind::Removed) {
    removedClips.push_back(clip);
  } else {
    clips.insert(clip);
  }
}

Projection::Projection(const DeviceRegistry &devices) : registry_(&devices) {}

void Projection::build(const model::Project &project,
                       const std::filesystem::path &mediaFolder) {
  if (!mediaFolder.is_absolute()) {
    throw std::invalid_argument("the media folder must be an absolute path");
  }
  mediaFolder_ = mediaFolder;
  buildAll(project);
}

void Projection::buildAll(const model::Project &project) {
  clear();
  try {
    setEngineDefaults();
    setTiming(project);
    masterStrip_ = insertOwn<StripGainPlugin>(edit_->getMasterPluginList(),
                                              "a strip gain");
    createMissingTracks(project);
    orderTracks(project);
    syncChains(project, {}, true);
    forEachTrack(project, [this](const model::Track &track) {
      for (const model::Device &device : track.devices) {
        syncDevice(device);
      }
    });
    for (const model::Device &device : project.master.devices) {
      syncDevice(device);
    }
    forEachArrangementClip(
        project, [&](const model::Track &track, const model::Clip &clip) {
          syncClip(project, track, clip);
        });
    syncMixer(project);
  } catch (...) {
    clear();
    throw;
  }
  forgetUnusedFiles();
  watchChangedTracks();
  graphStale_ = true;
  edit_->getUndoManager().clearUndoHistory();
  built_ = true;
}

void Projection::apply(const model::Project &project,
                       const model::ChangeSet &changes) {
  if (!built_) {
    throw std::logic_error("Projection::apply needs a built projection");
  }
  try {
    applyChanges(project, changes);
  } catch (...) {
    // Whatever threw left the edit somewhere between two documents. The
    // project says where it should be.
    buildAll(project);
    throw;
  }
}

void Projection::applyChanges(const model::Project &project,
                              const model::ChangeSet &changes) {
  Pending pending;
  for (const model::Change &change : changes.changes()) {
    pending.see(change);
  }

  const bool tempoChanged = pending.timing && project.tempo != tempo_;
  if (pending.timing) {
    setTiming(project);
  }
  for (const model::ClipId clip : pending.removedClips) {
    removeClip(clip);
  }
  for (const model::DeviceId device : pending.removedDevices) {
    removeDevice(device);
  }
  // New tracks first, so that what moved onto them finds them; tracks that
  // are gone last, so that what moved off them is taken out before.
  if (pending.trackList) {
    createMissingTracks(project);
  }
  syncChains(project, pending.chains, pending.allChains);
  syncDevices(project, pending.devices);
  syncClips(project, pending.clips);
  relink(project, pending.media);
  if (tempoChanged) {
    placeAllClips();
  }
  if (pending.mixer) {
    syncMixer(project);
  }
  if (pending.trackList) {
    removeGoneTracks(project);
    orderTracks(project);
  }
  forgetUnusedFiles();
  watchChangedTracks();
  edit_->getUndoManager().clearUndoHistory();
}

void Projection::setSampleRate(double sampleRate) {
  if (sampleRate == sampleRate_) {
    return;
  }
  sampleRate_ = sampleRate;
  placeAllClips();
  changedMidiTracks_.clear();
  edit_->getUndoManager().clearUndoHistory();
}

void Projection::clear() {
  for (auto &[id, entry] : clips_) {
    removeParts(entry);
  }
  releaseEngineObjects();
  for (auto *track : te::getAudioTracks(*edit_)) {
    edit_->deleteTrack(track);
  }
  edit_->getMasterPluginList().clear();
  edit_->getUndoManager().clearUndoHistory();
}

void Projection::releaseEngineObjects() {
  built_ = false;
  clips_.clear();
  midiClips_.clear();
  devices_.clear();
  tracks_.clear();
  masterStrip_ = nullptr;
  files_.clear();
  changedMidiTracks_.clear();
}

void Projection::setEngineDefaults() {
  // Everything the engine would otherwise decide for itself and that can be
  // heard. Its master volume plugin stays in the path at unity; the
  // master's level is the strip gain in front of it.
  edit_->setMasterVolumeSliderPos(te::decibelsToVolumeFaderPosition(0.0F));
  edit_->setMasterPanPos(0.0F);
  edit_->getMasterVolumePlugin()->setPanLaw(te::PanLawLinear);
  edit_->masterFadeIn = te::TimeDuration{};
  edit_->masterFadeOut = te::TimeDuration{};
  edit_->clickTrackEnabled = false;
  edit_->getTransport().looping = false;
}

void Projection::setTiming(const model::Project &project) {
  tempo_ = project.tempo;
  const auto &tempo = edit_->tempoSequence;
  tempo.getTempo(0)->setBpm(project.tempo.beatsPerMinute());
  auto *signature = tempo.getTimeSig(0);
  signature->numerator = project.timeSignature.numerator();
  signature->denominator = project.timeSignature.denominator();
}

void Projection::createMissingTracks(const model::Project &project) {
  forEachTrack(project, [this](const model::Track &track) {
    if (tracks_.contains(track.id)) {
      return;
    }
    TrackEntry entry;
    entry.kind = track.kind;
    entry.track = edit_->insertNewAudioTrack(
        te::TrackInsertPoint(nullptr, te::getTopLevelTracks(*edit_).getLast()),
        nullptr, false);
    requireMade(entry.track != nullptr, "a track");
    auto &list = entry.track->pluginList;
    if (track.kind == model::TrackKind::Midi) {
      entry.guard = insertOwn<NoteGuardPlugin>(list, "a note guard");
    }
    if (track.kind == model::TrackKind::Return) {
      entry.bus = nextBus_++;
      entry.input = insertPlugin(
          list, te::createValueTree(te::IDs::PLUGIN, te::IDs::type,
                                    te::AuxReturnPlugin::xmlTypeName,
                                    te::IDs::busNum, entry.bus));
      entry.strip = insertOwn<StripGainPlugin>(list, "a strip gain");
    } else {
      entry.strip = insertOwn<StripGainPlugin>(list, "a strip gain");
      entry.output = insertOwn<StripGainPlugin>(list, "a strip gain");
    }
    tracks_.emplace(track.id, std::move(entry));
  });
}

void Projection::removeGoneTracks(const model::Project &project) {
  std::erase_if(tracks_, [&](const auto &item) {
    if (model::findTrack(project, item.first) != nullptr) {
      return false;
    }
    edit_->deleteTrack(item.second.track.get());
    return true;
  });
}

void Projection::orderTracks(const model::Project &project) {
  std::vector<te::AudioTrack *> wanted;
  forEachTrack(project, [&](const model::Track &track) {
    wanted.push_back(tracks_.at(track.id).track.get());
  });
  for (std::size_t index = 0; index < wanted.size(); ++index) {
    auto *current =
        te::getAudioTracks(*edit_).getUnchecked(static_cast<int>(index));
    if (current == wanted.at(index)) {
      continue;
    }
    edit_->moveTrack(wanted.at(index), te::TrackInsertPoint(*current, true));
  }
}

void Projection::syncChains(const model::Project &project,
                            const std::set<model::TrackId> &only, bool all) {
  if (!all && only.empty()) {
    return;
  }
  forEachTrack(project, [&](const model::Track &track) {
    if (all || only.contains(track.id)) {
      syncChain(project, track);
    }
  });
  if (all || only.contains(project.master.id)) {
    syncMasterChain(project.master);
  }
}

void Projection::syncChain(const model::Project &project,
                           const model::Track &track) {
  TrackEntry &entry = tracks_.at(track.id);
  auto &list = entry.track->pluginList;
  std::vector<te::Plugin *> wanted;
  if (entry.guard != nullptr) {
    wanted.push_back(entry.guard.get());
  }
  if (entry.input != nullptr) {
    wanted.push_back(entry.input.get());
  }
  for (const model::Device &device : track.devices) {
    wanted.push_back(deviceFor(device, list));
  }
  wanted.push_back(entry.strip.get());
  if (entry.output != nullptr) {
    syncSends(project, entry);
    for (const Send &send : entry.sends) {
      wanted.push_back(send.plugin.get());
    }
    wanted.push_back(entry.output.get());
  }
  orderPlugins(list, wanted);
}

void Projection::syncMasterChain(const model::Track &master) {
  auto &list = edit_->getMasterPluginList();
  std::vector<te::Plugin *> wanted;
  wanted.reserve(master.devices.size() + 1);
  for (const model::Device &device : master.devices) {
    wanted.push_back(deviceFor(device, list));
  }
  wanted.push_back(masterStrip_.get());
  orderPlugins(list, wanted);
}

te::Plugin *Projection::deviceFor(const model::Device &device,
                                  te::PluginList &list) {
  if (const auto found = devices_.find(device.id); found != devices_.end()) {
    return found->second.plugin.get();
  }
  DeviceEntry entry;
  entry.binding = registry_->find(device.plugin);
  entry.plugin = insertPlugin(list, entry.binding != nullptr
                                        ? entry.binding->createState(device)
                                        : PlaceholderPlugin::create());
  return devices_.emplace(device.id, std::move(entry))
      .first->second.plugin.get();
}

void Projection::syncSends(const model::Project &project, TrackEntry &entry) {
  std::vector<Send> sends;
  for (const model::Track &target : project.returns) {
    const auto existing =
        std::ranges::find(entry.sends, target.id, &Send::target);
    if (existing != entry.sends.end()) {
      sends.push_back(*existing);
      entry.sends.erase(existing);
      continue;
    }
    const auto plugin = insertPlugin(
        entry.track->pluginList,
        te::createValueTree(te::IDs::PLUGIN, te::IDs::type,
                            te::AuxSendPlugin::xmlTypeName, te::IDs::busNum,
                            tracks_.at(target.id).bus));
    auto *send = dynamic_cast<te::AuxSendPlugin *>(plugin.get());
    requireMade(send != nullptr, "a send");
    sends.push_back({.target = target.id, .plugin = send});
  }
  // What is left sends to return tracks that are gone.
  for (const Send &send : entry.sends) {
    send.plugin->deleteFromParent();
  }
  entry.sends = std::move(sends);
}

void Projection::syncDevices(const model::Project &project,
                             const std::set<model::DeviceId> &devices) {
  if (devices.empty()) {
    return;
  }
  const auto syncIn = [&](const model::Track &track) {
    for (const model::Device &device : track.devices) {
      if (devices.contains(device.id)) {
        syncDevice(device);
      }
    }
  };
  forEachTrack(project, syncIn);
  syncIn(project.master);
}

void Projection::syncDevice(const model::Device &device) {
  const DeviceEntry &entry = devices_.at(device.id);
  entry.plugin->setEnabled(!device.bypassed);
  if (entry.binding != nullptr) {
    entry.binding->apply(*entry.plugin, device);
  }
}

void Projection::removeDevice(model::DeviceId device) {
  const auto found = devices_.find(device);
  if (found == devices_.end()) {
    return;
  }
  found->second.plugin->deleteFromParent();
  devices_.erase(found);
}

void Projection::syncMixer(const model::Project &project) {
  const SoloState solo = soloState(project);
  for (const model::Track &track : project.tracks) {
    const TrackEntry &entry = tracks_.at(track.id);
    entry.strip->setGains(
        stripGains(track.mixer, trackFeedsSends(track.mixer, solo)));
    const float direct = trackReachesMaster(track.mixer, solo) ? 1.0F : 0.0F;
    entry.output->setGains({.left = direct, .right = direct});
    // The engine's sends and the document's are both in return order.
    for (std::size_t index = 0; index < entry.sends.size(); ++index) {
      entry.sends.at(index).plugin->setGainDb(
          engineDb(track.mixer.sends.at(index).levelDb));
    }
  }
  for (const model::Track &track : project.returns) {
    tracks_.at(track.id).strip->setGains(
        stripGains(track.mixer, returnReachesMaster(track.mixer, solo)));
  }
  model::MixerStrip master = project.master.mixer;
  master.pan = 0.0;
  masterStrip_->setGains(stripGains(master, true));
}

void Projection::syncClips(const model::Project &project,
                           const std::set<model::ClipId> &clips) {
  if (clips.empty()) {
    return;
  }
  // One walk over the project finds them all; the document has no index.
  for (const model::Track &track : project.tracks) {
    for (const model::Slot &slot : track.slots) {
      if (slot.clip.has_value() && clips.contains(slot.clip->id)) {
        removeClip(slot.clip->id);
      }
    }
    for (const model::Clip &clip : track.arrangement) {
      if (clips.contains(clip.id)) {
        syncClip(project, track, clip);
      }
    }
  }
}

void Projection::syncClip(const model::Project &project,
                          const model::Track &track, const model::Clip &clip) {
  ClipEntry &entry = clips_[clip.id];
  if (entry.track != track.id) {
    removeParts(entry);
    entry.track = track.id;
  }
  if (const auto *midi = std::get_if<model::MidiContent>(&clip.content)) {
    MidiClipContent content{
        .extent = clip.extent,
        .loop = clip.loop,
        .notes = midi->notes,
    };
    // A change of name or colour leaves what the clip plays as it was.
    if (entry.parts.empty() ||
        std::get<MidiClipContent>(entry.content) != content) {
      entry.content = std::move(content);
      entry.events = nullptr;
      changedMidiTracks_.insert(track.id);
      graphStale_ = true;
    }
  } else {
    const auto &audio = std::get<model::AudioContent>(clip.content);
    const model::MediaReference &media = mediaOf(project, audio.media);
    setFile(entry, mediaFile(media));
    entry.content = AudioClipContent{
        .extent = clip.extent,
        .loop = clip.loop,
        .audio = audio,
        .fileSampleRate = media.sampleRate,
    };
  }
  placeClip(clip.id, entry);
}

void Projection::removeClip(model::ClipId clip) {
  const auto found = clips_.find(clip);
  if (found == clips_.end()) {
    return;
  }
  removeParts(found->second);
  filesChanged_ =
      filesChanged_ || !found->second.file.getFullPathName().isEmpty();
  clips_.erase(found);
}

void Projection::removeParts(ClipEntry &entry) {
  if (std::holds_alternative<MidiClipContent>(entry.content) &&
      !entry.parts.empty()) {
    changedMidiTracks_.insert(entry.track);
    graphStale_ = true;
  }
  for (const te::Clip::Ptr &part : entry.parts) {
    midiClips_.erase(part->itemID.getRawID());
    part->removeFromParent();
  }
  entry.parts.clear();
}

void Projection::setFile(ClipEntry &entry, const juce::File &file) {
  if (file != entry.file) {
    removeParts(entry);
    entry.file = file;
    filesChanged_ = true;
  }
}

void Projection::placeClip(model::ClipId id, ClipEntry &entry) {
  if (const auto *midi = std::get_if<MidiClipContent>(&entry.content)) {
    placeMidiClip(id, entry, *midi);
  } else {
    placeAudioClip(entry, std::get<AudioClipContent>(entry.content));
  }
}

void Projection::placeMidiClip(model::ClipId id, ClipEntry &entry,
                               const MidiClipContent &content) {
  const TimeMap time = timeMap();
  const std::int64_t clipStart = content.extent.position.ticks;
  const std::int64_t start = time.frame(clipStart);
  const std::int64_t end = std::min(
      time.frame(clipStart + content.extent.length.ticks), time.horizonFrame());
  if (start >= end) {
    removeParts(entry);
    return;
  }
  const std::int64_t guard = midiGuardFrames(sampleRate_);
  entry.originFrame = std::max<std::int64_t>(0, start - guard);
  const te::TimeRange range(
      te::TimePosition::fromSeconds(time.frameSeconds(entry.originFrame)),
      te::TimePosition::fromSeconds(time.frameSeconds(end + guard)));
  if (entry.parts.empty()) {
    const auto clip =
        tracks_.at(entry.track).track->insertMIDIClip({}, range, nullptr);
    requireMade(clip != nullptr, "a MIDI clip");
    // The engine's choice between event times in seconds and in beats hangs
    // on this switch; playbackSequence() answers in seconds.
    clip->setUsesProxy(true);
    midiClips_.emplace(clip->itemID.getRawID(), id);
    entry.parts.emplace_back(clip.get());
    changedMidiTracks_.insert(entry.track);
  }
  entry.parts.front()->setPosition({.time = range, .offset = {}});
}

void Projection::placeAudioClip(ClipEntry &entry,
                                const AudioClipContent &content) {
  const TimeMap time = timeMap();
  const FileState &file = fileState(entry.file);
  // The file's own rate counts, whatever the media reference states.
  AudioClipContent played = content;
  played.fileSampleRate = static_cast<std::uint32_t>(file.sampleRate);
  const std::vector<AudioPart> parts =
      file.playable() ? audioParts(played, time) : std::vector<AudioPart>{};
  if (parts.size() != entry.parts.size()) {
    removeParts(entry);
  }
  const double fileRate = file.sampleRate;
  for (std::size_t index = 0; index < parts.size(); ++index) {
    const AudioPart &part = parts.at(index);
    const te::ClipPosition position{
        .time =
            {
                te::TimePosition::fromSeconds(
                    time.frameSeconds(part.startFrame)),
                te::TimePosition::fromSeconds(
                    time.frameSeconds(part.startFrame + part.frameCount)),
            },
        .offset = te::TimeDuration::fromSeconds(
            static_cast<double>(part.firstSample -
                                (part.loop ? part.loop->start : 0)) /
            fileRate),
    };
    if (entry.parts.size() <= index) {
      const auto clip =
          tracks_.at(entry.track)
              .track->insertWaveClip({}, entry.file, position, false);
      requireMade(clip != nullptr, "an audio clip");
      entry.parts.emplace_back(clip.get());
    }
    auto &clip = dynamic_cast<te::WaveAudioClip &>(*entry.parts.at(index));
    // Everything the engine would otherwise decide from its settings or
    // from what it finds in the file.
    clip.setUsesProxy(false);
    clip.setAutoTempo(false);
    clip.setAutoPitch(false);
    clip.setTimeStretchMode(te::TimeStretcher::disabled);
    clip.setSpeedRatio(1.0);
    clip.setPitchChange(0.0F);
    clip.setIsReversed(false);
    clip.setAutoCrossfade(false);
    clip.setFadeIn({});
    clip.setFadeOut({});
    clip.setPan(0.0F);
    // At the device's rate the engine's plain converter hands the file's
    // samples through untouched. At any other rate that converter distorts
    // (about 4 % on a 1 kHz tone), and the best one the engine has is used.
    clip.setResamplingQuality(fileRate == sampleRate_
                                  ? te::ResamplingQuality::lagrange
                                  : te::ResamplingQuality::sincBest);
    // The engine's lowest clip gain is not silence; its mute is.
    clip.setMuted(content.audio.gainDb <= model::silenceDb);
    clip.setGainDB(static_cast<float>(content.audio.gainDb));
    if (part.loop) {
      clip.setLoopRange({te::TimePosition::fromSeconds(
                             static_cast<double>(part.loop->start) / fileRate),
                         te::TimePosition::fromSeconds(
                             static_cast<double>(part.loop->end) / fileRate)});
    } else {
      clip.disableLooping();
    }
    clip.setPosition(position);
  }
}

void Projection::placeAllClips() {
  for (auto &[id, entry] : clips_) {
    placeClip(id, entry);
    if (std::holds_alternative<MidiClipContent>(entry.content)) {
      changedMidiTracks_.insert(entry.track);
    }
  }
  graphStale_ = true;
}

void Projection::relink(const model::Project &project,
                        const std::set<model::MediaId> &media) {
  if (media.empty()) {
    return;
  }
  for (auto &[id, entry] : clips_) {
    const auto *audio = std::get_if<AudioClipContent>(&entry.content);
    if (audio == nullptr || !media.contains(audio->audio.media)) {
      continue;
    }
    const juce::File before = entry.file;
    setFile(entry, mediaFile(mediaOf(project, audio->audio.media)));
    if (before != entry.file) {
      placeClip(id, entry);
    }
  }
}

juce::File Projection::mediaFile(const model::MediaReference &media) const {
  std::filesystem::path path(media.path);
  if (path.is_relative()) {
    path = mediaFolder_ / path;
  }
  return {juce::String(path.lexically_normal().string())};
}

const Projection::FileState &Projection::fileState(const juce::File &file) {
  const std::string path = file.getFullPathName().toStdString();
  if (const auto known = files_.find(path); known != files_.end()) {
    return known->second;
  }
  FileState state;
  if (!file.existsAsFile()) {
    state.problem = MediaProblem::Missing;
    return files_.emplace(path, std::move(state)).first->second;
  }
  const te::AudioFile audio(edit_->engine, file);
  const te::AudioFileInfo info = audio.getInfo();
  state.sampleRate = info.sampleRate;
  if (!(info.sampleRate > 0.0) || info.lengthInSamples <= 0) {
    state.sampleRate = 0.0;
    state.problem = MediaProblem::Unreadable;
  } else if (info.needsCachedProxy) {
    // Read through a buffer that a background thread fills: what a render
    // gets of it would depend on how fast that thread is.
    state.problem = MediaProblem::UnsupportedFormat;
  } else {
    // The cache maps a file into memory from a thread of its own, some time
    // after the file is first asked for, and plays silence until then. Asked
    // with a negative timeout it does the mapping here and now. Holding the
    // reader keeps the mapping for as long as a clip uses the file.
    state.reader =
        edit_->engine.getAudioFileManager().cache.createReader(audio);
    requireMade(state.reader != nullptr, "a reader for an audio file");
    float ignoredLeftMax = 0.0F;
    float ignoredLeftMin = 0.0F;
    float ignoredRightMax = 0.0F;
    float ignoredRightMin = 0.0F;
    state.reader->setReadPosition(0);
    state.reader->getRange(1, ignoredLeftMax, ignoredLeftMin, ignoredRightMax,
                           ignoredRightMin, -1);
  }
  return files_.emplace(path, std::move(state)).first->second;
}

void Projection::forgetUnusedFiles() {
  if (!std::exchange(filesChanged_, false)) {
    return;
  }
  std::set<std::string> used;
  for (const auto &[id, entry] : clips_) {
    if (std::holds_alternative<AudioClipContent>(entry.content)) {
      used.insert(entry.file.getFullPathName().toStdString());
    }
  }
  std::erase_if(files_,
                [&](const auto &item) { return !used.contains(item.first); });
}

std::vector<MediaReport> Projection::mediaProblems() const {
  std::map<model::MediaId, MediaReport> reports;
  for (const auto &[id, entry] : clips_) {
    const auto *audio = std::get_if<AudioClipContent>(&entry.content);
    if (audio == nullptr) {
      continue;
    }
    const FileState &file =
        files_.at(entry.file.getFullPathName().toStdString());
    std::optional<MediaProblem> problem = file.problem;
    if (!problem.has_value() &&
        file.sampleRate != static_cast<double>(audio->fileSampleRate)) {
      problem = MediaProblem::SampleRateDiffers;
    }
    if (problem.has_value()) {
      reports.insert_or_assign(audio->audio.media,
                               MediaReport{
                                   .media = audio->audio.media,
                                   .problem = *problem,
                                   .fileSampleRate = file.sampleRate,
                               });
    }
  }
  std::vector<MediaReport> list;
  list.reserve(reports.size());
  for (const auto &[media, report] : reports) {
    list.push_back(report);
  }
  return list;
}

std::shared_ptr<const ClipEvents>
Projection::eventsOf(const ClipEntry &entry) const {
  const TimeMap time = timeMap();
  if (entry.events == nullptr || entry.eventsTime != time) {
    entry.events = std::make_shared<const ClipEvents>(
        noteEvents(std::get<MidiClipContent>(entry.content), time));
    entry.eventsTime = time;
  }
  return entry.events;
}

std::vector<model::ClipId> Projection::truncatedClips() const {
  std::vector<model::ClipId> clips;
  for (const auto &[id, entry] : clips_) {
    if (std::holds_alternative<MidiClipContent>(entry.content) &&
        eventsOf(entry)->truncated) {
      clips.push_back(id);
    }
  }
  return clips;
}

void Projection::watchChangedTracks() {
  const std::set<model::TrackId> changed =
      std::exchange(changedMidiTracks_, {});
  if (changed.empty() || !edit_->getTransport().isPlaying()) {
    return;
  }
  // Everything the changed tracks play now, by track. They are MIDI
  // tracks, so every clip on them is a MIDI clip.
  std::map<model::TrackId, std::vector<std::shared_ptr<const ClipEvents>>>
      played;
  for (const auto &[id, entry] : clips_) {
    if (changed.contains(entry.track) && !entry.parts.empty()) {
      played[entry.track].push_back(eventsOf(entry));
    }
  }
  for (const model::TrackId track : changed) {
    const auto entry = tracks_.find(track);
    if (entry == tracks_.end()) {
      continue;
    }
    std::vector<const ClipEvents *> clips;
    for (const auto &events : played[track]) {
      clips.push_back(events.get());
    }
    entry->second.guard->watch(std::make_shared<const NoteSchedule>(clips));
  }
}

void Projection::graphHandedOver() {
  for (const auto &[id, entry] : tracks_) {
    if (entry.guard != nullptr) {
      entry.guard->graphHandedOver();
    }
  }
}

void Projection::playbackRestarted() {
  changedMidiTracks_.clear();
  for (const auto &[id, entry] : tracks_) {
    if (entry.guard != nullptr) {
      entry.guard->forget();
    }
  }
}

std::optional<juce::MidiMessageSequence>
Projection::playbackSequence(const te::MidiClip &clip) const {
  const auto known = midiClips_.find(clip.itemID.getRawID());
  if (known == midiClips_.end()) {
    return std::nullopt;
  }
  ++sequenceRequests_;
  const ClipEntry &entry = clips_.at(known->second);
  const std::shared_ptr<const ClipEvents> events = eventsOf(entry);
  juce::MidiMessageSequence sequence;
  for (const NoteEvent &event : events->events) {
    const auto frames = static_cast<double>(event.frame - entry.originFrame);
    if (event.isNoteOn()) {
      sequence.addEvent(
          juce::MidiMessage::noteOn(midiChannel, event.pitch,
                                    static_cast<juce::uint8>(event.velocity)),
          (frames + noteOnOffsetFrames) / sampleRate_);
    } else {
      sequence.addEvent(juce::MidiMessage::noteOff(midiChannel, event.pitch),
                        (frames + noteOffOffsetFrames) / sampleRate_);
    }
  }
  return sequence;
}

} // namespace lina::engine::internal
