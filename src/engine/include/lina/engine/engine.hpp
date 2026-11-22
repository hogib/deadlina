// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_ENGINE_ENGINE_HPP
#define LINA_ENGINE_ENGINE_HPP

#include "lina/engine/audio_buffer.hpp"
#include "lina/engine/project_report.hpp"
#include "lina/model/change_set.hpp"
#include "lina/model/project.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lina::engine {

namespace internal {
struct TracktionAccess;
} // namespace internal

/// How an Engine is set up. Fixed for the lifetime of the Engine.
struct EngineConfig {
  /// Folder for the engine's settings, caches and temporary files. Created if
  /// it does not exist. The one thing the engine writes elsewhere is the set
  /// of LV2 specification files that its plugin host unpacks into the system
  /// temporary directory (TMPDIR) at start-up.
  std::filesystem::path storageFolder;

  /// Threads that process audio, counting the one that calls into the engine.
  /// With 1 the whole graph runs on the calling thread. The engine never
  /// uses more than the machine has processors.
  int audioThreads = 1;
};

/// Sample rate and block size of a stream of audio.
struct StreamFormat {
  double sampleRate = 48000.0;
  int blockSize = 512;
};

/// A stretch of the edit to render offline. Positions count sample frames at
/// `format.sampleRate` from the start of the edit.
struct RenderRequest {
  StreamFormat format;
  std::int64_t startFrame = 0;
  std::int64_t frameCount = 0;
};

/// The playback engine and the one edit it plays.
///
/// An Engine never opens an audio device of the machine. Audio leaves it in
/// two ways: through the manual device, which hands out exactly the frames
/// that process() is asked for, or through an offline render, which is the
/// same thing done for the caller. Both always produce `outputChannelCount`
/// channels.
///
/// Threads: an Engine is created, used and destroyed on the message thread,
/// while a MessageThread exists.
///
/// Errors come in two kinds, and every function keeps to this:
///   - A call that the program should not have made throws. A wrong thread
///     or a missing MessageThread and a call that needs a manual device when
///     none is open throw std::logic_error; a format, frame or count out of
///     range throws std::invalid_argument.
///   - A failure of the outside world, such as a file that cannot be written,
///     is returned as std::unexpected with a description.
class Engine {
public:
  static constexpr int outputChannelCount = 2;

  /// The longest stretch one render may cover, in frames. A 32-bit float WAV
  /// file of that length stays below the 4 GiB limit of the format.
  static constexpr std::int64_t maxRenderFrameCount = 500'000'000;

  /// Throws std::invalid_argument if the storage folder is empty.
  explicit Engine(const EngineConfig &config);
  ~Engine();

  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;
  Engine(Engine &&) = delete;
  Engine &operator=(Engine &&) = delete;

  /// Makes the engine play `project`: empties the edit and builds it anew
  /// from the document (docs/design/13-architecture.md, "The projection").
  /// The engine keeps no reference to `project`. A media reference with a
  /// relative path is looked for in `mediaFolder`, which must be an absolute
  /// path (std::invalid_argument otherwise).
  ///
  /// What is played of a document: the tempo, audio, MIDI and return tracks
  /// and the master with volume, pan, mute, solo and sends, devices whose
  /// plugin the engine knows (any other passes its input through), and
  /// arrangement clips. Audio clips play at the speed of their file,
  /// whether or not they ask to follow the tempo. Session clips, automation
  /// and controller data are not played yet.
  ///
  /// With a manual device open, the playback graph is brought up to date
  /// before this returns, as after applyChanges().
  ///
  /// `project` must keep the document's rules (model::validate), as the
  /// project of a Document always does. Where the engine depends on a rule
  /// that is broken, this throws std::invalid_argument. Whatever is thrown,
  /// the engine then has no project and an empty edit.
  void setProject(const model::Project &project,
                  const std::filesystem::path &mediaFolder);

  /// Brings the edit in step with an edit of the document. `changes` is
  /// what Document::apply, commit, undo or redo returned, and `project` the
  /// document after it; every change set must be passed on, in order.
  ///
  /// The call is complete in itself. With a manual device open, the
  /// playback graph is up to date when it returns: a stream that is playing
  /// goes on with the changed document from the next block, a note that the
  /// edit left without an end is ended, and nothing is left for the engine
  /// to do later from a timer. Without a device the edit is brought in step
  /// and the graph is built when playback starts.
  ///
  /// Throws std::logic_error before setProject(). Every other exception
  /// means that a part of the program the engine relies on is broken (a
  /// plugin binding that throws or names a plugin that cannot be made, a
  /// project that breaks the document's rules). The engine then rebuilds
  /// the edit from `project` and throws the exception again; if the rebuild
  /// fails as well, the engine has no project and an empty edit, which
  /// hasProject() tells.
  void applyChanges(const model::Project &project,
                    const model::ChangeSet &changes);

  /// True while the engine plays a project: after a setProject() that did
  /// not throw, until a failure leaves the engine without one.
  [[nodiscard]] bool hasProject() const;

  /// The media references of arrangement clips whose file is not as the
  /// document says: missing, not audio, in a format that is not played, or
  /// at another sample rate than the reference states. A file is looked at
  /// when a clip first uses its path, not again later.
  [[nodiscard]] std::vector<MediaReport> mediaProblems() const;

  /// The MIDI clips of the arrangement that would play more notes than the
  /// engine plays of one clip (250,000, counting every repetition of a
  /// loop) and are silent from there on.
  [[nodiscard]] std::vector<model::ClipId> truncatedClips() const;

  /// Attaches the edit to a device that produces audio only when process() is
  /// called. Replaces a manual device that is already open. Playback is
  /// stopped afterwards.
  void openManualDevice(const StreamFormat &format);

  /// Detaches the edit from the manual device. Does nothing without one.
  void closeManualDevice();

  [[nodiscard]] bool hasManualDevice() const;

  /// Rebuilds the playback graph from the edit as it is now and starts
  /// playback so that the first frame of the next process() call is
  /// `startFrame`, counted at the manual device's sample rate. Every device in
  /// the edit starts from its initial state: nothing of an earlier run is
  /// heard. A note of a MIDI clip that is sounding at `startFrame` starts on
  /// that frame (docs/design/08-midi-clips-and-note-editing.md, "Playback",
  /// rule 3). Needs a manual device.
  ///
  /// The first play() or render of an Engine blocks for about 0.2 s of real
  /// time: the engine underneath waits that long for a device that is not
  /// running by itself when its playback context is made. That happens once
  /// per Engine; nothing here waits or sleeps after it.
  void play(std::int64_t startFrame = 0);

  /// Stops playback. The manual device goes on producing blocks, of silence
  /// and of whatever the devices in the edit emit while stopped.
  void stop();

  [[nodiscard]] bool isPlaying() const;

  /// Makes the playback graph match the edit as it is now, without stopping:
  /// the playhead stays where it is and every device that is still in the
  /// edit keeps its state. The new graph is in use from the next block that
  /// process() runs. Needs a manual device; before the first play() there is
  /// no graph yet and nothing to do.
  ///
  /// For edits made to the engine's edit by hand. setProject() and
  /// applyChanges() do this themselves.
  ///
  /// The engine does the same on its own some time after an edit changes,
  /// from a timer on the message thread; this does it now, and that timer
  /// then has nothing left to do.
  void syncGraph();

  /// Runs the manual device for `frameCount` frames, in blocks of the device's
  /// block size plus one shorter block if the count does not divide, and
  /// returns what it produced. Uses no clock and does not sleep: the stream
  /// advances by exactly the frames asked for. Needs a manual device.
  [[nodiscard]] AudioBuffer process(std::int64_t frameCount);

  /// Renders `request.frameCount` frames of the edit from
  /// `request.startFrame` and returns them.
  ///
  /// A render is, by definition and by construction, what block-driven
  /// playback gives: a manual device is opened with `request.format`, play()
  /// starts at `request.startFrame` and process() runs for the frames asked
  /// for. So a render that starts part-way into the edit is not a slice of a
  /// render from the beginning. Devices start from their initial state at
  /// `startFrame`: a note that is held across it starts again there, and the
  /// tail of a delay or reverb fed before it is absent.
  ///
  /// Works with and without a manual device open and whatever that device's
  /// format is. Afterwards playback is stopped. If a manual device was open
  /// it is open again with its own format, as after openManualDevice(): a
  /// stream that was playing does not resume and has to be started with
  /// play().
  ///
  /// Throws std::invalid_argument unless `startFrame` is at least 0 and
  /// `frameCount` is between 1 and maxRenderFrameCount.
  [[nodiscard]] AudioBuffer renderOffline(const RenderRequest &request);

  /// Like renderOffline, but writes a 32-bit floating-point WAV file instead
  /// of holding the audio in memory.
  ///
  /// The audio goes to a temporary file next to `file`, named `file` plus
  /// ".lina-partial", which takes the place of `file` only when it is
  /// complete. On failure an existing `file` is left as it was and the
  /// temporary file is removed. Folders leading to `file` are created.
  [[nodiscard]] std::expected<void, std::string>
  renderToWavFile(const RenderRequest &request,
                  const std::filesystem::path &file);

private:
  friend struct internal::TracktionAccess;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace lina::engine

#endif // LINA_ENGINE_ENGINE_HPP
