// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format: how the document spells the enumerations of the
// model. Enumerations are written as these names, never as numbers, so the
// order of enumerators in the model is free to change. A name, once released,
// is never reused for something else.
#ifndef LINA_FORMAT_INTERNAL_NAMES_HPP
#define LINA_FORMAT_INTERNAL_NAMES_HPP

#include "internal/strict_reader.hpp"
#include "lina/model/project.hpp"

#include <array>

namespace lina::format::detail {

// The tables below pair an enumerator with its spelling, one pair per line.
// Naming the two members in every row would only add noise.
// NOLINTBEGIN(modernize-use-designated-initializers)

inline constexpr std::array launchQuantisationNames{
    EnumName{model::LaunchQuantisation::None, "none"},
    EnumName{model::LaunchQuantisation::Bars8, "8_bars"},
    EnumName{model::LaunchQuantisation::Bars4, "4_bars"},
    EnumName{model::LaunchQuantisation::Bars2, "2_bars"},
    EnumName{model::LaunchQuantisation::Bar1, "1_bar"},
    EnumName{model::LaunchQuantisation::Half, "1/2"},
    EnumName{model::LaunchQuantisation::Quarter, "1/4"},
    EnumName{model::LaunchQuantisation::Eighth, "1/8"},
    EnumName{model::LaunchQuantisation::Sixteenth, "1/16"},
    EnumName{model::LaunchQuantisation::ThirtySecond, "1/32"},
};

inline constexpr std::array trackKindNames{
    EnumName{model::TrackKind::Audio, "audio"},
    EnumName{model::TrackKind::Midi, "midi"},
    EnumName{model::TrackKind::Return, "return"},
    EnumName{model::TrackKind::Master, "master"},
};

inline constexpr std::array monitoringNames{
    EnumName{model::Monitoring::Off, "off"},
    EnumName{model::Monitoring::On, "on"},
    EnumName{model::Monitoring::Auto, "auto"},
};

inline constexpr std::array deviceRoleNames{
    EnumName{model::DeviceRole::Instrument, "instrument"},
    EnumName{model::DeviceRole::AudioEffect, "audio_effect"},
};

inline constexpr std::array controllerKindNames{
    EnumName{model::ControllerKind::PitchBend, "pitch_bend"},
    EnumName{model::ControllerKind::ChannelPressure, "channel_pressure"},
    EnumName{model::ControllerKind::ControlChange, "control_change"},
};

inline constexpr std::array sourceTempoOriginNames{
    EnumName{model::SourceTempoOrigin::File, "file"},
    EnumName{model::SourceTempoOrigin::Detected, "detected"},
    EnumName{model::SourceTempoOrigin::Guessed, "guessed"},
    EnumName{model::SourceTempoOrigin::Manual, "manual"},
};

inline constexpr std::array stretchModeNames{
    EnumName{model::StretchMode::Stretch, "stretch"},
    EnumName{model::StretchMode::Repitch, "repitch"},
};

inline constexpr std::array mixerParameterNames{
    EnumName{model::MixerParameter::Volume, "volume"},
    EnumName{model::MixerParameter::Pan, "pan"},
};

// The "type" member of the objects that stand for a variant of the model.
namespace type_name {
inline constexpr std::string_view midi = "midi";
inline constexpr std::string_view audio = "audio";
inline constexpr std::string_view none = "none";
inline constexpr std::string_view allMidi = "all_midi";
inline constexpr std::string_view midiDevice = "midi_device";
inline constexpr std::string_view mixer = "mixer";
inline constexpr std::string_view send = "send";
inline constexpr std::string_view deviceParameter = "device_parameter";
} // namespace type_name

// NOLINTEND(modernize-use-designated-initializers)

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_NAMES_HPP
