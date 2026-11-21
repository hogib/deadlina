// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_model: the rules of the model, written once. `validate` runs
// them over a whole project; commands run the ones that an edit could break
// before they commit it.
#ifndef LINA_MODEL_SRC_RULES_HPP
#define LINA_MODEL_SRC_RULES_HPP

#include "lina/model/project.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace lina::model::detail {

using Failures = std::vector<Failure>;

// True for the kinds of track that hold clips: audio and MIDI.
bool isRegular(TrackKind kind);

// True for a value the document may store inside [low, high]: finite, and not
// negative zero, which would compare equal to zero and yet be written
// differently.
bool inRange(double value, double low, double high);

// True for text a document may store: well-formed UTF-8 (no overlong forms,
// no surrogates, nothing past U+10FFFF) without the character U+0000.
bool isStorableText(std::string_view text);

// True for a content hash as a media reference stores it: 64 lower-case
// hexadecimal digits.
bool isContentHash(std::string_view text);

// True for a device's state reference: empty for "no stored state", or the
// name of a state file, which is a content hash followed by ".bin".
bool isStateRef(std::string_view text);

// True when the document can still hand out `count` identities without its
// counter passing `maxNextId`.
bool idsAvailable(const Project &project, std::uint64_t count);

// True when the value is one of the enumerators. A loader that casts a number
// from a file to an enumeration relies on these through `validate`.
bool isKnown(TrackKind value);
bool isKnown(Monitoring value);
bool isKnown(LaunchQuantisation value);
bool isKnown(StretchMode value);
bool isKnown(SourceTempoOrigin value);
bool isKnown(ControllerKind value);
bool isKnown(DeviceRole value);
bool isKnown(MixerParameter value);

// The order the model keeps notes in: by start, then pitch.
bool noteBefore(const Note &a, const Note &b);
void sortNotes(std::vector<Note> &notes);

// Value ranges, order and same-pitch overlap of a clip's notes.
void checkNotes(const std::vector<Note> &notes, Failures &failures);

// A MIDI clip's controller data: ranges, order, one envelope per controller.
void checkControllers(const std::vector<ControllerEnvelope> &controllers,
                      ObjectRef clip, Failures &failures);

// An audio clip's content.
void checkAudio(const Project &project, const AudioContent &audio,
                ObjectRef clip, Failures &failures);

// A clip's own properties and whether its content suits the track: everything
// but the content itself.
void checkClipHeader(const Clip &clip, TrackKind trackKind, bool inSlot,
                     Failures &failures);

// Everything about one clip that does not depend on its neighbours.
void checkClip(const Project &project, const Clip &clip, TrackKind trackKind,
               bool inSlot, Failures &failures);

// True when a clip with this extent would overlap an arrangement clip other
// than `self`.
bool overlapsOther(const std::vector<Clip> &arrangement,
                   const ClipExtent &extent, ClipId self);

// Puts arrangement clips into the order the model keeps them in: by position.
void sortArrangement(std::vector<Clip> &arrangement);

// Instrument placement and parameter lists of a device chain.
void checkDevices(TrackKind trackKind, const std::vector<Device> &devices,
                  ObjectRef track, Failures &failures);

// The track's own kind, colour and mixer strip: what a property edit can break.
void checkTrackHeader(const Project &project, const Track &track,
                      Failures &failures);

// One envelope against the track it is on: the target exists there, and the
// points are in order and inside the target's range. Not that the target is
// free.
void checkEnvelope(const Project &project, const Track &track,
                   const Envelope &envelope, Failures &failures);

// Everything about one track except that its identities are unique in the
// project.
void checkTrack(const Project &project, const Track &track, Failures &failures);

void checkMedia(const MediaReference &media, Failures &failures);
void checkScene(const Scene &scene, Failures &failures);
void checkLoopRange(const LoopRange &loop, Failures &failures);
void checkStartMarker(TickPosition position, Failures &failures);
void checkLaunchQuantisation(LaunchQuantisation quantisation,
                             Failures &failures);

} // namespace lina::model::detail

#endif // LINA_MODEL_SRC_RULES_HPP
