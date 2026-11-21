# 14 Build, test and CI

Status: **draft**. Stack-dependent. This doc turns the testing and maintainability rules in
`CLAUDE.md` into concrete tooling. Items are *proposed* unless marked decided.

## Toolchain

| Item | Choice | Note |
|---|---|---|
| Language | C++23 (decided) | Headers, not modules. Qt's `moc` handles module units only from Qt 6.13 with CMake 4.5, and not yet with GCC. Revisit then. |
| Build system | CMake with Ninja (decided) | JUCE and Tracktion Engine support nothing else. |
| Presets | `CMakePresets.json` | One preset per configuration below, so that local runs and CI use the same commands. |
| Compilers | GCC and Clang, both required to build cleanly | Verified 2026-11-18 with GCC 16.2 and Clang 23.1: the whole stack builds at C++23 on both, with the two workarounds listed under "Dependencies". Clang uses libstdc++, to stay compatible with the system's Qt. |
| Formatting | `clang-format`, LLVM style (decided) | Checked in CI. |
| Static analysis | `clang-tidy` with a checked-in `.clang-tidy` | Our targets only. |

## Dependencies

| Dependency | How it is obtained | Status |
|---|---|---|
| Tracktion Engine v3.2.0, with JUCE 8.0.6 as its submodule | Git submodule pinned to the release tag | Decided 2026-11-13 |
| Qt 6.12 LTS (Core, Gui, Qml, Quick, QuickControls2, QuickTest) | System packages | Decided as a dependency |
| Catch2 | System package if present, otherwise fetched by CMake at v3.16.1, pinned by commit | Approved in `CLAUDE.md` |
| nlohmann/json | System package, 3.12 or newer; parsing only (doc 16) | Approved 2026-11-13 |
| SoundTouch | Bundled with Tracktion Engine | Decided 2026-11-13 as the time-stretch backend |

- JUCE and Tracktion compile once into a static library with their warnings silenced, and are
  included as system headers so that our warning flags do not fire on them.
- No third-party source is patched (`CLAUDE.md`). Tracktion's vendored `nanorange.hpp` has a
  typo that GCC 14 and later reject; the workaround is the flag `-Wno-template-body` on the
  third-party target only.
- **Clang workaround (decided 2026-11-18).** JUCE 8.0.6 has a
  never-instantiated constructor template in `juce_AudioPluginInstance.h` whose body does not
  compile. GCC accepts it; Clang 23 rejects it at every language level. Under Clang, every
  translation unit that includes JUCE is therefore compiled with `-fdelayed-template-parsing`.
  - Cost: in those files, Clang does not check the bodies of templates that are never
    instantiated, so Clang diagnostics, `clang-tidy` and coverage do not see them. GCC still
    does. `lina_model` and `lina_format` never include JUCE and are unaffected.
  - Risk: Clang deprecates this mode after C++20 and may remove it. The remaining ways out would
    be a JUCE version that fixes the header, or a patch, which the project rules forbid.
- **UBSan workaround (decided 2026-11-18).** In the `asan` preset
  the third-party library is built with UBSan's function-type check off. JUCE's bundled LV2
  libraries call a function through a pointer of a slightly different type when an engine
  starts, and that check aborts on it. All other UBSan checks and ASan stay on for third-party
  code, and LINA's own code keeps every check.
- Build options set for JUCE: web browser and curl off, JACK off for now, VST3 and LV2 hosting
  on, LADSPA off. JUCE's application and plugin wrappers are not built.
- In debug builds JUCE assertions are routed through its logger, so that tests can fail when one
  fires. In release builds assertions are compiled out, and a test's "no assertion fired" check
  proves nothing there.
- Starting an engine with LV2 hosting on makes JUCE read every LV2 bundle installed on the
  machine. Engine tests point `HOME` at a scratch folder so the user's own plugins stay out; the
  system-wide LV2 folders cannot be redirected, so tests must not depend on what is found there.

### Time-stretch

Doc 07 requires stretching in the first release. At the Tracktion tag used so far, all stretch
backends are off by default and the choices are:

| Backend | Bundled with Tracktion | Licence as stated in the checkout | Note |
|---|---|---|---|
| SoundTouch | yes | LGPL 2.1 or later | No new dependency. Its tempo detector is already compiled in. |
| Rubber Band | no | GPL, licensed separately | A new system dependency. Generally regarded as higher quality. |
| Elastique | no | proprietary | Not usable in a GPL project. |

Decided 2026-11-13: SoundTouch for the first release, since it adds no dependency. Rubber Band is
reconsidered if its quality turns out not to be good enough.

## Our compile flags

- `-Wall -Wextra -Wpedantic -Werror` on every LINA target (`CLAUDE.md`: warnings are errors).
- `QT_NO_KEYWORDS`, because Tracktion has identifiers named `slots`.
- Every library of ours is added through one CMake function, `lina_add_library`, so none can
  miss the warnings or the coverage instrumentation.
- The CMake option `LINA_ENABLE_ENGINE` (on by default) can be switched off to build and test
  the model tier alone, without compiling the third-party library. CI always builds with it on.
- CMake's scanning for C++ modules is switched off, consistent with "headers, not modules".
- Functions that run on the audio thread are marked `[[clang::nonblocking]]`.

## Configurations

| Preset | Compiler | Purpose |
|---|---|---|
| `dev` | GCC, debug | Everyday work |
| `release` | GCC, optimised | What ships; performance benches |
| `asan` | Clang, ASan + UBSan | Whole test suite |
| `tsan` | Clang, TSan | Whole test suite |
| `rtsan` | Clang 20+, RealtimeSanitizer | Tests that drive the audio callback |
| `coverage` | Clang, source-based coverage | Coverage ratchet |

- **Risk:** RealtimeSanitizer will also see Tracktion's and JUCE's audio-thread code. The fit
  check found at least one place where the engine takes a lock related to the audio callback
  (tempo changes). Expect to need a suppression list for third-party frames, kept in the repo
  and reviewed when the engine is upgraded.

## Test tiers

| Tier | Covers | Framework | Needs |
|---|---|---|---|
| Model | Commands, inverses, model rules, exact time arithmetic | Catch2 | Nothing; no Qt, no JUCE |
| Format | Round trip, byte-stable save, migrations, golden files per format version, damaged input | Catch2, plus a fuzz target | Nothing |
| Projection | Incremental projection equals a from-scratch build, for generated command sequences (doc 13) | Catch2 | Engine, no audio device |
| Audio | Offline render of small projects compared with checked-in reference audio within a stated tolerance | Catch2 | Engine, no audio device |
| Session timing | Launch boundaries, scene launch, stop, record rounding, at the exact sample | Catch2 | Engine driven block by block |
| Interface | QML components and custom items with synthesized input | Qt Quick Test, offscreen | Qt; `QT_QUICK_BACKEND=rhi` must be set or the offscreen platform silently picks the software renderer |
| Benches | The targets of doc 11 on the reference machine | Own harness | Not part of the gating suite at first |

- Tests never open a real audio device, read the wall clock, sleep, use the network or use
  unseeded randomness (`CLAUDE.md`). The engine runs on a null device or is driven by hand.
- Two exceptions to "no wall clock", both forced by the frameworks:
  - Tests of the event-loop bridge wait for real events (timers, calls from other threads).
    Every such wait blocks in the event loop until a condition holds and fails the test on a
    long deadline. No test measures elapsed time.
  - Tracktion waits about 0.2 seconds when its playback context is created under a device
    driven by hand (doc 13). No test result depends on it.
- A fuzz target for the project file reader is built when `LINA_BUILD_FUZZERS` is on (Clang
  only). It is not part of the gating suite; its seed corpus is replayed by an ordinary test.
- Longer runs outside the gating suite: `LINA_PROJECTION_SOAK=<n>` multiplies the projection
  oracle's sequences; `lina_projection_bench` measures edits on a large project.
- `LINA_UPDATE_AUDIO_REFS=1` rewrites the reference audio and fails the test on purpose.
  `tests/audio-refs/tone.flac` was made with `flac --best` from a 0.25 second 1 kHz tone at
  48 kHz, 16 bit; JUCE's bundled FLAC encoder trips UBSan and is not used in tests.
- ThreadSanitizer: one suppression, in the engine tests' support code, for a write-only static
  inside JUCE's memory-mapped reader that two engines in one process both touch.
- Engine and bridge tests set `HOME` and `TMPDIR` to a scratch folder and unset `DISPLAY`
  themselves, so a test binary run by hand behaves as it does under CTest.
- Reference audio comparisons state their tolerance per test. A test that depends on a
  third-party plugin is not allowed in the gating suite; a small test instrument and effect of
  our own are built for the purpose.
- Golden project files live in `tests/golden/v<N>/` and are never edited after the format
  version that wrote them is released.

## Coverage ratchet

1. The `coverage` preset runs the gating suite and produces line and branch coverage with
   `llvm-cov`, for LINA's own sources under `src/` only. A source file under `src/` that no test
   executable links fails the run, since it would otherwise be invisible to the measurement.
   Until the first library source exists the stored baseline is `none`, not a number; the commit
   that adds the first source records the first real figures.
2. The current figures are stored in a small checked-in file. CI fails if either figure is lower
   than the stored one. It also fails if a figure is higher, until the file is updated in the
   same commit, so the stored figures never lag behind.
3. Excluded from the figure: generated code (`moc`, QML type registration) and third-party code.

## Continuous integration

1. Every push and pull request runs: format check, `dev` and `release` builds with GCC, `asan`,
   `tsan`, `rtsan`, `coverage` with the ratchet, `clang-tidy`.
   - Until LINA has audio-thread functions marked real-time, the `rtsan` job proves only that
     the stack builds and starts with that runtime.
2. All jobs run in the official Arch Linux container image, pinned by digest, and install their
   packages from the Arch Linux Archive snapshot of one fixed day, which pins the compiler and
   CMake versions (and Qt, once it is used). The pins live in the workflow file. Installing in
   every job is slower than a prebuilt image; a prebuilt image would need a registry to publish
   it to.
3. The third-party static library is cached between runs, keyed by the pinned commit and the
   compiler version.
4. CI runs on GitHub Actions (decided 2026-11-13). The workflow is added with the first buildable
   code, not before.

## Tools

The project's tooling carries silly names (`CLAUDE.md`, "Naming"). Decided 2026-11-18.

| Script | CMake target | Name | Does |
|---|---|---|---|
| `tools/grid.sh [--fix]` | `grid` | "GRID Rejects Indentation Deviance" | Checks every C++ file of ours against `.clang-format`; `--fix` reformats in place. |
| `tools/flat.sh [build-dir]` | `flat` (Clang presets only) | "FLAT Lints All Translation-units" | Runs `clang-tidy` with the checked-in config over our translation units. |
| `tools/pawl.sh [--update]` | none | "PAWL Always Wants Lines" | The coverage ratchet below; `--update` rewrites `tools/coverage-baseline.txt`. |

## Repository layout

```
CMakeLists.txt  CMakePresets.json  .clang-format  .clang-tidy
cmake/                               (CMake modules: third-party library, Qt, warnings, sanitizers)
tools/                               (grid, flat, pawl: see "Tools")
.github/workflows/ci.yml
src/model/  src/format/  src/engine/  src/bridge/  src/ui/  src/app/
  <library>/include/lina/<library>/  (public headers)
  engine/internal/                   (headers for code inside the engine tier and its tests)
qml/
tests/model/  tests/format/  tests/engine/  tests/bridge/  tests/ui/  tests/golden/
tests/audio-refs/
  model/support/                     (command generator and change-set oracle, also for WP4)
  engine/support/                    (test instrument and effect, edit building, scratch folders)
third_party/tracktion_engine/        (submodule)
docs/design/
```

## Open questions

1. `clang-tidy`'s analyzer reports a finding inside a Catch2 header when `REQUIRE_FALSE` is used
   in a loop, despite the header filter. Tests write `REQUIRE(!x)` until that is understood.

(The spike sources were removed from the tree on 2026-11-18; they remain in git
history at commit `389c082`.)
