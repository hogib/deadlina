// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/engine.hpp"

#include "lina/engine/internal/device_registry.hpp"
#include "lina/engine/internal/engine_plugins.hpp"
#include "lina/engine/internal/projection.hpp"
#include "lina/engine/internal/tracktion_access.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace lina::engine {

namespace {

namespace te = tracktion;

/// Keeps the engine's settings and caches in the folder the caller chose.
class FolderPropertyStorage final : public te::PropertyStorage {
public:
  explicit FolderPropertyStorage(juce::File folder)
      : PropertyStorage("LINA"), folder_(std::move(folder)) {}

  juce::File getAppCacheFolder() override { return folder_; }
  juce::File getAppPrefsFolder() override { return folder_; }

private:
  juce::File folder_;
};

/// Where LINA decides instead of the engine.
class Behaviour final : public te::EngineBehaviour {
public:
  explicit Behaviour(int audioThreads) : audioThreads_(audioThreads) {}

  // The engine never looks at the machine's audio devices on its own.
  bool autoInitialiseDeviceManager() override { return false; }
  bool addSystemAudioIODeviceTypes() override { return false; }
  int getNumberOfCPUsToUseForAudio() override { return audioThreads_; }

  // The document has no such limits (the engine's own are 400 tracks, 1500
  // clips on a track, 16 plugins on a track and 4 on the master).
  te::EditLimits getEditLimits() override {
    constexpr int unlimited = 1'000'000;
    return {
        .maxNumTracks = unlimited,
        .maxClipsInTrack = unlimited,
        .maxPluginsOnClip = 0,
        .maxPluginsOnTrack = unlimited,
        .maxNumMasterPlugins = unlimited,
    };
  }

  // A beat is a quarter note whatever the time signature
  // (docs/design/04-transport-and-musical-time.md).
  bool lengthOfOneBeatDependsOnTimeSignature() override { return false; }

  // The projection places every clip itself when the tempo changes; the
  // engine must not move anything on its own.
  bool areAudioClipsRemappedWhenTempoChanges() override { return false; }
  bool areAutoTempoClipsRemappedWhenTempoChanges() override { return false; }
  bool areMidiClipsRemappedWhenTempoChanges() override { return false; }
  bool arePluginsRemappedWhenTempoChanges() override { return false; }

  // Audio clips play their file itself, never a copy the engine renders in
  // the background, and get no fades of the engine's choosing.
  bool autoAddClipEdgeFades() override { return false; }
  ClipDefaults getClipDefaults() override {
    return {
        .useProxyFile = false,
        .resamplingQuality = te::ResamplingQuality::lagrange,
    };
  }

private:
  int audioThreads_;
};

juce::File toJuceFile(const std::filesystem::path &path) {
  return {juce::String(std::filesystem::absolute(path).string())};
}

/// Every entry point of an Engine starts here: JUCE and Tracktion assert, and
/// in optimised builds silently misbehave, when they are used off the
/// message thread or before JUCE is initialised.
void requireMessageThread(const char *caller) {
  const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
  if (messages == nullptr || !messages->isThisTheMessageThread()) {
    throw std::logic_error(std::string(caller) +
                           " must be called on the message thread, while a "
                           "lina::engine::MessageThread exists");
  }
}

/// Checks what an Engine needs before any part of it is built, and returns
/// its storage folder.
juce::File prepareStorageFolder(const std::filesystem::path &folder) {
  requireMessageThread("Engine::Engine");
  if (folder.empty()) {
    throw std::invalid_argument("EngineConfig::storageFolder is empty");
  }
  std::filesystem::create_directories(folder);
  return toJuceFile(folder);
}

std::unique_ptr<te::Edit> createEdit(te::Engine &engine,
                                     const juce::File &storage) {
  const auto state = te::createEmptyEdit(engine);
  const auto id = te::ProjectItemID::fromProperty(state, te::IDs::projectID);
  // The edit is a projection and is never saved (docs/design/13); the file
  // only gives the engine a place to resolve relative paths against.
  const auto file = storage.getChildFile("projection.tracktionedit");
  te::Edit::Options options{
      .engine = engine,
      .editState = state,
      .editProjectItemID = id,
      .role = te::Edit::forEditing,
      .loadContext = nullptr,
      .numUndoLevelsToStore = 1,
      .editFileRetriever =
          [path = file.getFullPathName()] { return juce::File(path); },
      .filePathResolver = {},
      .numAudioTracks = 0,
      .defaultMasterVolumedB = 0.0F,
  };
  return te::Edit::createEdit(std::move(options));
}

te::TimePosition toTime(std::int64_t frame, double sampleRate) {
  return te::TimePosition::fromSamples(frame, sampleRate);
}

void validate(const StreamFormat &format) {
  if (!(format.sampleRate > 0.0) || format.blockSize <= 0) {
    throw std::invalid_argument(
        "StreamFormat needs a positive sample rate and block size");
  }
}

void validate(const RenderRequest &request) {
  validate(request.format);
  if (request.startFrame < 0 || request.frameCount < 1 ||
      request.frameCount > Engine::maxRenderFrameCount) {
    throw std::invalid_argument(
        "RenderRequest needs a start frame of at least 0 and between 1 and "
        "Engine::maxRenderFrameCount frames");
  }
}

std::string cannotWrite(const std::filesystem::path &file,
                        const std::string &reason) {
  return "cannot write " + file.string() + ": " + reason;
}

/// Removes the file of that name when it goes, which is how a render that
/// fails or throws leaves nothing behind. After a successful render the file
/// has been renamed and there is nothing to remove.
class PartialFile {
public:
  explicit PartialFile(std::filesystem::path path) : path_(std::move(path)) {}
  ~PartialFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  PartialFile(const PartialFile &) = delete;
  PartialFile &operator=(const PartialFile &) = delete;
  PartialFile(PartialFile &&) = delete;
  PartialFile &operator=(PartialFile &&) = delete;

private:
  std::filesystem::path path_;
};

/// Stops the edit and takes its playback graph down, which deinitialises
/// every plugin: nothing of an earlier run, such as the contents of a delay
/// line, is left for the next one.
///
/// The playback context itself is kept for as long as the Engine lives,
/// across runs and across devices. Making one is not free: the engine waits
/// for the audio device to move on by a block, checking once a millisecond
/// and up to 200 times (DeviceManager::addContext), and a manual device
/// moves only when process() is called.
void stopAndReleaseGraph(const te::Edit &edit) {
  auto &transport = edit.getTransport();
  transport.stop(false, false);
  if (auto *context = transport.getCurrentPlaybackContext()) {
    context->clearNodes();
  }
}

/// Starts the edit on the open manual device, whose format is `format`.
/// Builds the playback graph once and leaves nothing for the engine's timer.
void startPlayback(te::Edit &edit, internal::Projection &projection,
                   const StreamFormat &format, std::int64_t startFrame) {
  stopAndReleaseGraph(edit);
  projection.playbackStarting();
  auto &transport = edit.getTransport();
  transport.setPosition(toTime(startFrame, format.sampleRate));
  // A rebuild the edit still asks for (after edits made without a device)
  // builds the graph here, at the start frame.
  edit.dispatchPendingUpdatesSynchronously();
  // Otherwise the graph is built now. This makes the playback context if
  // this is the first run, which must not happen before a device is open: a
  // context made without one computes its stream times from a sample rate
  // of zero.
  transport.ensureContextAllocated(false);
  transport.play(false);
}

/// Runs the open manual device, whose format is `format`.
AudioBuffer runDevice(const te::Engine &engine, const StreamFormat &format,
                      std::int64_t frameCount) {
  AudioBuffer result;
  result.sampleRate = format.sampleRate;
  result.channels.assign(
      Engine::outputChannelCount,
      std::vector<float>(static_cast<std::size_t>(frameCount)));

  auto &host = engine.getDeviceManager().getHostedAudioDeviceInterface();
  juce::MidiBuffer midi;
  for (std::int64_t done = 0; done < frameCount;) {
    const auto count = static_cast<int>(
        std::min<std::int64_t>(format.blockSize, frameCount - done));
    std::array<float *, Engine::outputChannelCount> pointers{};
    for (std::size_t channel = 0; channel < pointers.size(); ++channel) {
      pointers.at(channel) = std::span(result.channels.at(channel))
                                 .subspan(static_cast<std::size_t>(done))
                                 .data();
    }
    juce::AudioBuffer<float> block(pointers.data(), Engine::outputChannelCount,
                                   count);
    host.processBlock(block, midi);
    done += count;
  }
  return result;
}

/// How many frames a render to a file holds in memory at a time.
constexpr std::int64_t fileRenderChunkFrames = 65536;

} // namespace

struct Engine::Impl {
  explicit Impl(const EngineConfig &config)
      : storage(prepareStorageFolder(config.storageFolder)),
        projection(devices),
        engine(std::make_unique<FolderPropertyStorage>(storage), nullptr,
               std::make_unique<Behaviour>(std::max(1, config.audioThreads))),
        edit(createEdit(engine, storage)) {
    auto &plugins = engine.getPluginManager();
    plugins.createBuiltInType<internal::StripGainPlugin>();
    plugins.createBuiltInType<internal::PlaceholderPlugin>();
    plugins.createBuiltInType<internal::SequencerPlugin>();
    projection.attach(*edit);
  }

  /// Makes the playback graph match the edit now, if the engine says it is
  /// out of date, and leaves nothing for the engine's own timer to do. Then
  /// hands the sequencers what their tracks play now. With the manual device
  /// the new graph and the new sequences are taken up by the same block;
  /// with a real device the graph can reach the audio thread a block before
  /// the sequences, and the sequencers reconcile at the next block
  /// (Projection::publish()).
  void syncAfterEdit() {
    if (device) {
      edit->dispatchPendingUpdatesSynchronously();
    }
    projection.publish();
  }

  void openDevice(const StreamFormat &format);
  void closeDevice();
  /// The format of the open manual device. `caller` is named in the
  /// std::logic_error thrown when there is none.
  [[nodiscard]] StreamFormat requireDevice(const char *caller) const;

  class RenderDevice;

  juce::File storage;
  internal::DeviceRegistry devices;
  /// The engine objects it holds are let go before the edit is destroyed
  /// (~Engine).
  internal::Projection projection;
  te::Engine engine;
  std::unique_ptr<te::Edit> edit;
  std::optional<StreamFormat> device;
};

/// The manual device of a render: open with the render's format while the
/// object lives, after which the caller's own device, or none, is back.
class Engine::Impl::RenderDevice {
public:
  RenderDevice(Impl &impl, const StreamFormat &format)
      : impl_(&impl), previous_(impl.device) {
    impl.openDevice(format);
  }
  ~RenderDevice() {
    if (previous_) {
      impl_->openDevice(*previous_);
    } else {
      impl_->closeDevice();
    }
  }

  RenderDevice(const RenderDevice &) = delete;
  RenderDevice &operator=(const RenderDevice &) = delete;
  RenderDevice(RenderDevice &&) = delete;
  RenderDevice &operator=(RenderDevice &&) = delete;

private:
  Impl *impl_;
  std::optional<StreamFormat> previous_;
};

void Engine::Impl::openDevice(const StreamFormat &format) {
  closeDevice();

  auto &devices = engine.getDeviceManager();
  te::HostedAudioDeviceInterface::Parameters parameters;
  parameters.sampleRate = format.sampleRate;
  parameters.blockSize = format.blockSize;
  parameters.useMidiDevices = false;
  parameters.inputChannels = 0;
  parameters.outputChannels = outputChannelCount;

  auto &host = devices.getHostedAudioDeviceInterface();
  host.initialise(parameters);
  host.prepareToPlay(format.sampleRate, format.blockSize);
  devices.dispatchPendingUpdates();
  // The engine mutes its output when a block takes longer to compute than to
  // play. That reads the wall clock, and a manual device has no deadline.
  devices.setCpuLimitBeforeMuting(std::numeric_limits<double>::max());

  device = format;
  // Clips sit on the sample grid of the device that plays them.
  projection.setSampleRate(format.sampleRate);
  projection.publish();
}

void Engine::Impl::closeDevice() {
  if (!device) {
    return;
  }
  stopAndReleaseGraph(*edit);

  auto &devices = engine.getDeviceManager();
  devices.deviceManager.closeAudioDevice();
  devices.removeHostedAudioDeviceInterface();
  device.reset();
}

StreamFormat Engine::Impl::requireDevice(const char *caller) const {
  if (!device) {
    throw std::logic_error(std::string(caller) + " needs a manual device");
  }
  return *device;
}

Engine::Engine(const EngineConfig &config)
    : impl_(std::make_unique<Impl>(config)) {}

Engine::~Engine() {
  impl_->closeDevice();
  impl_->projection.releaseEngineObjects();
  impl_->edit.reset();
}

void Engine::setProject(const model::Project &project,
                        const std::filesystem::path &mediaFolder) {
  requireMessageThread("Engine::setProject");
  try {
    impl_->projection.build(project, mediaFolder);
  } catch (...) {
    impl_->syncAfterEdit();
    throw;
  }
  impl_->syncAfterEdit();
}

void Engine::applyChanges(const model::Project &project,
                          const model::ChangeSet &changes) {
  requireMessageThread("Engine::applyChanges");
  try {
    impl_->projection.apply(project, changes);
  } catch (...) {
    impl_->syncAfterEdit();
    throw;
  }
  impl_->syncAfterEdit();
}

bool Engine::hasProject() const { return impl_->projection.isBuilt(); }

std::vector<MediaReport> Engine::mediaProblems() const {
  return impl_->projection.mediaProblems();
}

std::vector<model::ClipId> Engine::truncatedClips() const {
  return impl_->projection.truncatedClips();
}

void Engine::openManualDevice(const StreamFormat &format) {
  requireMessageThread("Engine::openManualDevice");
  validate(format);
  impl_->openDevice(format);
}

void Engine::closeManualDevice() {
  requireMessageThread("Engine::closeManualDevice");
  impl_->closeDevice();
}

bool Engine::hasManualDevice() const { return impl_->device.has_value(); }

void Engine::play(std::int64_t startFrame) {
  requireMessageThread("Engine::play");
  const auto format = impl_->requireDevice("Engine::play");
  if (startFrame < 0) {
    throw std::invalid_argument("Engine::play: negative start frame");
  }
  startPlayback(*impl_->edit, impl_->projection, format, startFrame);
}

void Engine::stop() {
  requireMessageThread("Engine::stop");
  impl_->edit->getTransport().stop(false, false);
}

bool Engine::isPlaying() const {
  return impl_->edit->getTransport().isPlaying();
}

void Engine::syncGraph() {
  requireMessageThread("Engine::syncGraph");
  std::ignore = impl_->requireDevice("Engine::syncGraph");
  // The engine's own route, taken at once instead of from its timer: the
  // first call marks the graph as out of date and starts that timer, the
  // second does the timer's work and stops it.
  impl_->edit->restartPlayback();
  impl_->edit->dispatchPendingUpdatesSynchronously();
  impl_->projection.publish();
}

AudioBuffer Engine::process(std::int64_t frameCount) {
  requireMessageThread("Engine::process");
  const auto format = impl_->requireDevice("Engine::process");
  if (frameCount < 0) {
    throw std::invalid_argument("Engine::process: negative frame count");
  }
  return runDevice(impl_->engine, format, frameCount);
}

AudioBuffer Engine::renderOffline(const RenderRequest &request) {
  requireMessageThread("Engine::renderOffline");
  validate(request);
  const Impl::RenderDevice device(*impl_, request.format);
  startPlayback(*impl_->edit, impl_->projection, request.format,
                request.startFrame);
  return runDevice(impl_->engine, request.format, request.frameCount);
}

std::expected<void, std::string>
Engine::renderToWavFile(const RenderRequest &request,
                        const std::filesystem::path &file) {
  requireMessageThread("Engine::renderToWavFile");
  validate(request);

  const auto target = std::filesystem::absolute(file);
  auto partial = target;
  partial += ".lina-partial";
  std::error_code error;
  if (std::filesystem::is_directory(target, error)) {
    return std::unexpected(cannotWrite(target, "it is a folder"));
  }
  std::filesystem::create_directories(target.parent_path(), error);
  if (error) {
    return std::unexpected(cannotWrite(target, error.message()));
  }
  // A file of that name is what a render that was cut short left behind.
  // Anything else is not ours to remove.
  if (std::filesystem::exists(partial, error) &&
      !std::filesystem::is_regular_file(partial, error)) {
    return std::unexpected(
        cannotWrite(target, partial.string() + " is in the way"));
  }
  std::filesystem::remove(partial, error);
  auto stream = std::make_unique<juce::FileOutputStream>(toJuceFile(partial));
  if (error || !stream->openedOk()) {
    return std::unexpected(
        cannotWrite(target, "could not create " + partial.string()));
  }
  const PartialFile cleanup(partial);

  auto *const wav = impl_->engine.getAudioFileFormatManager().getWavFormat();
  std::unique_ptr<juce::AudioFormatWriter> writer(wav->createWriterFor(
      stream.get(), request.format.sampleRate, outputChannelCount, 32, {}, 0));
  if (writer == nullptr) {
    return std::unexpected(
        cannotWrite(target, "the WAV writer refused the format"));
  }
  // The writer has taken the stream over, which is JUCE's ownership idiom.
  std::ignore = stream.release();

  {
    const Impl::RenderDevice device(*impl_, request.format);
    startPlayback(*impl_->edit, impl_->projection, request.format,
                  request.startFrame);
    for (std::int64_t left = request.frameCount; left > 0;) {
      const auto count = std::min(left, fileRenderChunkFrames);
      const auto audio = runDevice(impl_->engine, request.format, count);
      std::array<const float *, outputChannelCount> pointers{};
      for (std::size_t channel = 0; channel < pointers.size(); ++channel) {
        pointers.at(channel) = audio.channels.at(channel).data();
      }
      if (!writer->writeFromFloatArrays(pointers.data(), outputChannelCount,
                                        static_cast<int>(count))) {
        return std::unexpected(cannotWrite(target, "writing failed"));
      }
      left -= count;
    }
  }
  // Closing the writer completes the file's header.
  writer.reset();

  // What the file's own header says is the check that it is complete. The
  // reader takes its stream over only if it can read the file.
  auto input = std::make_unique<juce::FileInputStream>(toJuceFile(partial));
  const std::unique_ptr<juce::AudioFormatReader> written(
      wav->createReaderFor(input.get(), false));
  if (written != nullptr) {
    std::ignore = input.release();
  }
  if (written == nullptr || written->lengthInSamples != request.frameCount) {
    return std::unexpected(
        cannotWrite(target, "the file written is not complete"));
  }
  std::filesystem::rename(partial, target, error);
  if (error) {
    return std::unexpected(cannotWrite(target, error.message()));
  }
  return {};
}

namespace internal {

tracktion::Engine &TracktionAccess::engine(Engine &engine) {
  return engine.impl_->engine;
}

tracktion::Edit &TracktionAccess::edit(Engine &engine) {
  return *engine.impl_->edit;
}

DeviceRegistry &TracktionAccess::devices(Engine &engine) {
  return engine.impl_->devices;
}

const Projection &TracktionAccess::projection(Engine &engine) {
  return engine.impl_->projection;
}

} // namespace internal

} // namespace lina::engine
