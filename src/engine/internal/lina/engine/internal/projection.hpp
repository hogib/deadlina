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
//     MIDI track has LINA's sequencer (sequencer.hpp) in front of all of them.
//   - The plugins of a return track are the engine's bus input, its devices
//     and the strip gain.
//   - The master's devices and its strip gain are the edit's master plugins.
//     The engine's own master volume stays at unity.
//   - An audio clip is up to two engine clips (audio_parts.hpp). A MIDI clip
//     has no engine clip: what it plays is worked out here (sequence.hpp) and
//     handed to the sequencer of its track by publish(). A clip in a session
//     slot has nothing.
//   - The engine's tempo is the document's, for the devices that follow it.
//     Its time signature stays at 4/4: nothing in the engine is meant to use
//     it (a beat is a quarter note, the click is off), and a change of it would
//     make the engine rebuild its playback graph.
//
// Every time the engine is given is on a sample frame of the open device
// (time_map.hpp), so the projection has to be told the device's sample rate.
//
// Edits while playing. An edit of notes, of MIDI clips or of the mixer changes
// nothing in the engine's edit that would make it rebuild its playback graph:
// the projection works out what the MIDI tracks it touched play now, and the
// mixer's levels are values the audio thread reads. What the sequencer does
// with a new sequence while playing is NotePlayer's (note_player.hpp): a note
// that has lost its end is ended, and a note that an edit puts under the
// playhead is heard from its next start. Edits of audio clips, devices,
// tracks, sends and the tempo change the engine's edit, and the engine
// rebuilds its graph.
//
// Graph builds. The engine asks for one more rebuild of its graph whenever it
// builds a graph that holds a track without listeners (AudioTrack::addListener,
// called by each track's live MIDI node), which is every graph built from
// nothing, as for play. The projection keeps a listener of its own on every
// engine track, which does nothing, so that the engine never asks.
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
#include "lina/engine/internal/sequence.hpp"
#include "lina/engine/internal/sequencer.hpp"
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
    /// MIDI tracks only: first in the chain, and the clips it plays.
    SequencerPlugin::Ptr sequencer;
    std::set<model::ClipId> midiClips;
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
    /// Audio clips only: the engine clips, in playing order. Empty when
    /// nothing of the clip can be played: it lies behind the engine's
    /// horizon, or its file is not playable.
    std::vector<tracktion::Clip::Ptr> parts;
    /// MIDI clips only: what the clip plays under the current time map.
    std::shared_ptr<const ClipPlayback> playback;
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

  /// Hands the sequencer of every MIDI track whose clips have changed since
  /// the last call what the track plays now. The Engine calls it at the end
  /// of every call that changes the projection, after the engine's playback
  /// graph has been rebuilt for it. With the manual device, which runs no
  /// block in between, the new graph and the new sequences are taken up by
  /// the same block. With a real device the engine may hand the audio thread
  /// the new graph before this runs; the sequencers then play the old
  /// sequences for that block, and NotePlayer reconciles what sounds with the
  /// new sequence at the next block, so no note is left hanging.
  void publish();

  /// To be called when playback starts anew, with the playback graph down:
  /// the first block played chases the notes sounding across it.
  void playbackStarting();

  /// Lets go of every engine object without touching the edit, which is
  /// about to be destroyed.
  void releaseEngineObjects();

  [[nodiscard]] bool isBuilt() const { return built_; }
  [[nodiscard]] TimeMap timeMap() const { return {tempo_, sampleRate_}; }

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

private:
  struct Pending;

  /// Listens to an engine track and does nothing (see "Graph builds").
  class QuietListener final : public tracktion::AudioTrack::Listener {
  public:
    void injectLiveMidiMessage(
        [[maybe_unused]] tracktion::AudioTrack &track,
        [[maybe_unused]] const tracktion::MidiMessageWithSource &message,
        [[maybe_unused]] bool &wasUsed) override {}
    void recordedMidiMessageSentToPlugins(
        [[maybe_unused]] tracktion::AudioTrack &track,
        [[maybe_unused]] const juce::MidiMessage &message) override {}
  };

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
  static void removeParts(ClipEntry &entry);
  void setFile(ClipEntry &entry, const juce::File &file);
  void syncMidiClip(model::ClipId id, ClipEntry &entry,
                    const model::Track &track, MidiClipContent content);
  /// Takes a MIDI clip off the track it was on.
  void leaveTrack(model::ClipId id, const ClipEntry &entry);
  void placeClip(model::ClipId id, ClipEntry &entry);
  void placeAudioClip(ClipEntry &entry, const AudioClipContent &content);
  void placeAllClips();
  void relink(const model::Project &project,
              const std::set<model::MediaId> &media);
  [[nodiscard]] juce::File mediaFile(const model::MediaReference &media) const;
  const FileState &fileState(const juce::File &file);
  void forgetUnusedFiles();

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
  std::map<std::string, FileState> files_;
  /// True when the set of files that audio clips play may have changed.
  bool filesChanged_{false};
  /// The MIDI tracks whose clips changed since the last publish().
  std::set<model::TrackId> unpublished_;
  int nextBus_{0};
  QuietListener quietListener_;
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_PROJECTION_HPP
