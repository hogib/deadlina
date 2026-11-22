# LINA

**LINA** ("Lina is not Ableton") is a free digital audio workstation for Linux, built around two
linked ways of working: a grid of clips that are launched in time with each other for sketching
and performing, and a linear timeline for arranging and finishing.

Linux has capable timeline-first DAWs. It does not have a free one where the clip grid is the
centre of the design. LINA is meant to fill that gap.

## Status

**Early implementation. There is no application to run yet.**

The repository holds the design documents, the project rules, the build and test scaffolding,
and four libraries:

- the document model: musical time, tracks, clips, notes, undoable commands;
- the project file format: a canonical text document, a strict reader, and safe saving with
  backups, a recovery copy and a lock;
- the engine: the playback engine driven without an audio device, with rendering to a buffer
  or a file;
- the projection: the document turned into sound, for arrangement clips and the mixer, kept up
  to date edit by edit.

There is no interface yet, and no session view. All design documents are drafts.

## Building

Requirements: Linux, CMake 3.28 or newer, Ninja, GCC and Clang with C++23 support (developed with
GCC 16 and Clang 23), Qt 6.12 (base), nlohmann/json 3.12, and the development packages for ALSA, FreeType, Fontconfig and X11.

```sh
git clone https://github.com/hogib/deadlina.git
cd deadlina
# Tracktion Engine refers to JUCE by an SSH address; fetch it over HTTPS instead.
git -c url.https://github.com/.insteadOf=git@github.com: submodule update --init --recursive

cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Other presets: `release`, and on Clang `asan`, `tsan`, `rtsan` and `coverage`. See
[`docs/design/14-build-test-and-ci.md`](docs/design/14-build-test-and-ci.md).

## Where to look

- [`docs/design/`](docs/design/README.md): the design documents, starting with an index.
  - [Vision and scope](docs/design/02-vision-and-scope.md): what LINA is for and what the first
    release contains.
  - [Clean-room policy](docs/design/01-clean-room.md): what the project may and may not draw on.
  - [Architecture](docs/design/13-architecture.md): how the pieces fit together.
- [`CLAUDE.md`](CLAUDE.md): the working rules for the codebase (formatting, testing,
  maintainability, platforms).

## Planned first release

Audio and MIDI tracks, a session grid with quantised clip launching and scenes, a basic
arrangement, a mixer with sends and return tracks, recording, tempo-synchronised audio clips with
tempo detection, a note editor, automation, export, and hosting of third-party VST3 and LV2
plugins. No built-in instruments or effects at first; those come later.

## Technology

C++23, CMake, [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) on
[JUCE](https://juce.com) as the playback engine, and Qt 6 Quick for the interface. LINA keeps its
own document model and file format; the engine is used for playback only.

Linux is the only supported platform. A macOS port may be considered later. Windows is not
planned.

## Clean room

LINA is an independent project. It is not affiliated with, endorsed by, or derived from Ableton
or Ableton Live. It uses no Ableton code, assets, content or documentation text, and its visual
design is its own. "Ableton" and "Live" are trademarks of Ableton AG, mentioned here only to say
what LINA is not. The full policy is in
[`docs/design/01-clean-room.md`](docs/design/01-clean-room.md).

## Licence

LINA's own code is licensed under the GNU General Public License, version 3 or any later version
(`GPL-3.0-or-later`); see [`LICENSE`](LICENSE). It builds on Tracktion Engine (GPLv3 or later)
and JUCE (AGPLv3), so a distributed build is a combined work under those licences' terms.
