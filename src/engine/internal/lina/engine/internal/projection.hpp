// SPDX-License-Identifier: GPL-3.0-or-later
//
// The projection (docs/design/13-architecture.md): LINA's document turned
// into the engine's edit, one way. It builds the edit from a project and
// keeps it in step from change sets, and it holds the map from LINA's
// identities to the engine's objects. Nothing is ever read back from the
// edit into the document, and the edit's own undo history is kept empty.
//
// How the edit is laid out:
//   - One engine track per audio, MIDI and return track, in the project's
//     order: audio and MIDI tracks first, then return tracks.
//   - The plugins of an audio or MIDI track are its devices, the strip gain
//     (volume, pan, mute and solo), one send per return track in return
//     order, and a second gain that silences the track's direct output when
//     another track's solo calls for that while the sends keep running. A
//     MIDI track has a note guard in front of all of them.
//   - The plugins of a return track are the engine's bus input, its devices
//     and the strip gain.
//   - The master's devices and its strip gain are the edit's master plugins.
//     The engine's own master volume stays at unity.
//   - A MIDI clip is one engine clip that only marks a time range; what it
//     plays comes from playbackSequence(). An audio clip is up to two engine
//     clips (audio_parts.hpp). A clip in a session slot has none.
//
// Every time the engine is given is on a sample frame of the open device
// (time_map.hpp), so the projection has to be told the device's sample rate.
//
// Edits while playing. The first plugin of every MIDI track is a note guard
// (note_guard.hpp), which ends the notes an edit leaves without a note-off.
// A clip that comes to a track while playing (added, moved there, undone
// back) is new to the engine, which starts the notes sounding at the
// playhead. A note that an edit puts under the playhead inside a clip that
// is already playing is not started; it is heard from its next start.
//
// Audio files. A file is looked at when a clip first uses its path: whether
// it is there, whether it is audio the engine can map into memory, and what
// its sample rate really is (mediaProblems()). That is not repeated: a file
// that appears on disk later is picked up only when its media reference is
// relinked to another path or the project is set anew. A file at another
// rate than the device's is converted by the engine while it plays, with its
// best converter; where such a clip starts depends on the block size by up
// to about a frame.
//
// Failure. apply() either brings the edit in step or, when anything throws
// on the way, rebuilds the edit from the project it was given and throws
// again. When that rebuild throws too, the projection is not built any more
// and the edit is empty.
//
// Not projected, and ignored when a change set names them: scenes, slots and
// the clips in them, automation envelopes, controller data in MIDI clips,
// names and colours, the loop range and the start marker, launch
// quantisation, arm, monitoring and track inputs. Not projected either:
// following the tempo, stretching and transposing of audio clips
// (audio_parts.hpp), the short automatic fade at the edges of an audio clip
// (docs/design/06-arrangement-workflow.md, "Clips in the arrangement", rule
// 5) and the crossfade at an audio loop's join
// (docs/design/07-audio-clips-and-tempo-sync.md, "Looping and playback
// range", rule 2): edges and joins are hard cuts.
#ifndef LINA_ENGINE_INTERNAL_PROJECTION_HPP
#define LINA_ENGINE_INTERNAL_PROJECTION_HPP

#include "lina/engine/internal/audio_parts.hpp"
#include "lina/engine/internal/device_registry.hpp"
#include "lina/engine/internal/engine_plugins.hpp"
#include "lina/engine/internal/midi_events.hpp"
#include "lina/engine/internal/note_guard.hpp"
#include "lina/engine/internal/time_map.hpp"
#include "lina/engine/project_report.hpp"
#include "lina/model/change_set.hpp"
#include "lina/model/project.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace lina::engine::internal {

class Projection {
public:
  /// The send from one track to one return track.
  struct Send {
    model::TrackId target;
    juce::ReferenceCountedObjectPtr<tracktion::AuxSendPlugin> plugin;
  };

  /// What stands for one audio, MIDI or return track.
  struct TrackEntry {
    model::TrackKind kind{model::TrackKind::Audio};
    tracktion::AudioTrack::Ptr track;
    /// MIDI tracks only: first in the chain.
    NoteGuardPlugin::Ptr guard;
    StripGainPlugin::Ptr strip;
    /// Audio and MIDI tracks only: the gain after the sends.
    StripGainPlugin::Ptr output;
    /// Return tracks only: where the sends arrive, and the bus they use.
    tracktion::Plugin::Ptr input;
    int bus{-1};
    /// Audio and MIDI tracks only, in return order.
    std::vector<Send> sends;
  };

  struct DeviceEntry {
    tracktion::Plugin::Ptr plugin;
    /// Null for a placeholder.
    const DeviceBinding *binding{nullptr};
  };

  /// What was found when an audio file was looked at.
  struct FileState {
    /// Empty when the file can be played as the document says.
    std::optional<MediaProblem> problem;
    /// The sample rate found in the file; 0 when it could not be read.
    double sampleRate{0.0};
    /// Keeps the engine's file cache from letting go of the file's mapping.
    /// Null for a file that is not played.
    tracktion::AudioFileCache::Reader::Ptr reader;

    [[nodiscard]] bool playable() const { return reader != nullptr; }
  };

  /// What stands for one arrangement clip.
  struct ClipEntry {
    model::TrackId track;
    std::variant<MidiClipContent, AudioClipContent> content;
    /// Audio clips only: the file played.
    juce::File file;
    /// The engine clips, in playing order. Empty when nothing of the clip
    /// can be played: it lies behind the engine's horizon, or its file is
    /// not playable.
    std::vector<tracktion::Clip::Ptr> parts;
    /// MIDI clips only: the frame the engine clip starts on, from which its
    /// event times count.
    std::int64_t originFrame{0};

  private:
    friend class Projection;
    /// MIDI clips only: what the content plays under one time map, kept so
    /// that a clip nobody touched is not worked out again. Nothing but a
    /// memory of a pure function's result.
    mutable std::shared_ptr<const ClipEvents> events;
    mutable std::optional<TimeMap> eventsTime;
  };

  /// A projection of nothing yet: attach() gives it its edit.
  explicit Projection(const DeviceRegistry &devices);

  /// The edit to project into. Call once, before anything else.
  void attach(tracktion::Edit &edit) { edit_ = &edit; }

  /// Empties the edit and builds it from `project`. A relative path of a
  /// media reference is resolved against `mediaFolder`, which must be an
  /// absolute path (std::invalid_argument otherwise). When anything throws,
  /// the edit is empty and the projection not built.
  void build(const model::Project &project,
             const std::filesystem::path &mediaFolder);

  /// Brings the edit in step with `project`, the document after the edit
  /// that `changes` describes. Throws std::logic_error before build(). For
  /// any other failure see "Failure" above.
  void apply(const model::Project &project, const model::ChangeSet &changes);

  /// The sample rate of the device the edit plays on, which must be
  /// positive. Changing it moves every clip onto the new sample grid.
  void setSampleRate(double sampleRate);

  /// True, once, when what MIDI clips play has changed in a way the engine
  /// cannot see in its edit: the playback graph has to be rebuilt.
  [[nodiscard]] bool takeGraphStale() {
    return std::exchange(graphStale_, false);
  }

  /// To be called when the rebuilt playback graph has been handed to the
  /// audio thread: the note guards stop watching soon after.
  void graphHandedOver();

  /// To be called when playback starts anew, with the playback graph down.
  void playbackRestarted();

  /// Lets go of every engine object without touching the edit, which is
  /// about to be destroyed.
  void releaseEngineObjects();

  [[nodiscard]] bool isBuilt() const { return built_; }
  [[nodiscard]] TimeMap timeMap() const { return {tempo_, sampleRate_}; }

  /// What a projected MIDI clip plays, with times in seconds from the start
  /// of the engine clip; empty for a clip the projection did not make.
  [[nodiscard]] std::optional<juce::MidiMessageSequence>
  playbackSequence(const tracktion::MidiClip &clip) const;

  /// The events of a projected MIDI clip under the current time map.
  [[nodiscard]] std::shared_ptr<const ClipEvents>
  eventsOf(const ClipEntry &entry) const;

  /// The media references of arrangement clips whose file is not as the
  /// document says, in order of identity.
  [[nodiscard]] std::vector<MediaReport> mediaProblems() const;

  /// The MIDI clips that hold more notes than one clip plays
  /// (maxSoundingNotesPerClip) and are silent past that number.
  [[nodiscard]] std::vector<model::ClipId> truncatedClips() const;

  // --- For tests: the map from LINA's identities to engine objects ---

  [[nodiscard]] const std::map<model::TrackId, TrackEntry> &tracks() const {
    return tracks_;
  }
  [[nodiscard]] const std::map<model::DeviceId, DeviceEntry> &devices() const {
    return devices_;
  }
  [[nodiscard]] const std::map<model::ClipId, ClipEntry> &clips() const {
    return clips_;
  }
  [[nodiscard]] const StripGainPlugin::Ptr &masterStrip() const {
    return masterStrip_;
  }
  /// The audio files looked at for the clips in use, by full path.
  [[nodiscard]] const std::map<std::string, FileState> &files() const {
    return files_;
  }
  /// How often the engine has asked for a clip's events, which it does once
  /// per MIDI clip whenever it rebuilds its playback graph.
  [[nodiscard]] std::uint64_t sequenceRequests() const {
    return sequenceRequests_;
  }

private:
  struct Pending;

  void applyChanges(const model::Project &project,
                    const model::ChangeSet &changes);
  void buildAll(const model::Project &project);
  void clear();
  void setEngineDefaults();
  void setTiming(const model::Project &project);

  void createMissingTracks(const model::Project &project);
  void removeGoneTracks(const model::Project &project);
  void orderTracks(const model::Project &project);

  void syncChains(const model::Project &project,
                  const std::set<model::TrackId> &only, bool all);
  void syncChain(const model::Project &project, const model::Track &track);
  void syncMasterChain(const model::Track &master);
  tracktion::Plugin *deviceFor(const model::Device &device,
                               tracktion::PluginList &list);
  void syncSends(const model::Project &project, TrackEntry &entry);
  void syncDevices(const model::Project &project,
                   const std::set<model::DeviceId> &devices);
  void syncDevice(const model::Device &device);
  void removeDevice(model::DeviceId device);
  void syncMixer(const model::Project &project);

  void syncClips(const model::Project &project,
                 const std::set<model::ClipId> &clips);
  void syncClip(const model::Project &project, const model::Track &track,
                const model::Clip &clip);
  void removeClip(model::ClipId clip);
  void removeParts(ClipEntry &entry);
  void setFile(ClipEntry &entry, const juce::File &file);
  void placeClip(model::ClipId id, ClipEntry &entry);
  void placeMidiClip(model::ClipId id, ClipEntry &entry,
                     const MidiClipContent &content);
  void placeAudioClip(ClipEntry &entry, const AudioClipContent &content);
  void placeAllClips();
  void relink(const model::Project &project,
              const std::set<model::MediaId> &media);
  [[nodiscard]] juce::File mediaFile(const model::MediaReference &media) const;
  const FileState &fileState(const juce::File &file);
  void forgetUnusedFiles();
  /// Hands the note guards of the tracks whose MIDI changed what those
  /// tracks play now, if anything is playing.
  void watchChangedTracks();

  tracktion::Edit *edit_{nullptr};
  const DeviceRegistry *registry_;
  bool built_{false};
  double sampleRate_{48'000.0};
  model::Tempo tempo_;
  std::filesystem::path mediaFolder_;

  std::map<model::TrackId, TrackEntry> tracks_;
  std::map<model::DeviceId, DeviceEntry> devices_;
  std::map<model::ClipId, ClipEntry> clips_;
  StripGainPlugin::Ptr masterStrip_;
  /// Engine clip to MIDI clip, for playbackSequence().
  std::map<std::uint64_t, model::ClipId> midiClips_;
  std::map<std::string, FileState> files_;
  /// True when the set of files that audio clips play may have changed.
  bool filesChanged_{false};
  /// The MIDI tracks whose notes changed since the guards were last told.
  std::set<model::TrackId> changedMidiTracks_;
  bool graphStale_{false};
  int nextBus_{0};
  mutable std::uint64_t sequenceRequests_{0};
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_PROJECTION_HPP
