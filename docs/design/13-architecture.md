# 13 Architecture

Status: **draft**. This doc is stack-dependent. Decided by the project owner on 2026-11-13: C++23,
CMake, Tracktion Engine on JUCE as a runtime playback engine, Qt 6 Quick for the interface, and
LINA owning its own document. Everything else here is *proposed* unless marked decided.

Evidence referred to below comes from throwaway spikes and a source-level fit check of docs 03 to
10 against Tracktion Engine v3.2.0. The spikes were deleted once their measurements were recorded;
the sources of the two C++ spikes remain in git history at commit `389c082` under `spike/`.

Engine version, decided 2026-11-13: Tracktion Engine release tag v3.2.0, which pins JUCE 8.0.6.
This is what every spike and the fit check used. Its `develop` branch (JUCE 9, unreleased) is not
used; LINA moves when a new release is tagged.

## The central idea

LINA's document is its own data structure. The playback engine is given a projection of it and is
never the source of truth.

```
gesture ──► command ──► document model ──► change set ──┬──► interface (redraw what changed)
                         (exact, undoable)              └──► projection ──► Tracktion edit ──► audio
                                                                               │
engine state (playhead, slot states, meters) ◄─────────────────────────────────┘
        read by the interface every frame; never written into the document
```

Why (decided 2026-11-13): the engine stores positions as floating-point seconds and beats, has
its own file format with user names and timestamps, and gives notes no stored identity. Docs 04
and 10 require the opposite.

## Libraries and their dependencies

Each is a separate CMake target. An arrow means "may include headers of".

```
lina_app ──► lina_ui ──► lina_bridge ──► lina_engine ──► lina_model
                │             │                              ▲
                └─────────────┴──────────────► lina_format ──┘
```

| Library | Contains | May use | Must not use |
|---|---|---|---|
| `lina_model` | Document types, exact musical time, identities, commands, undo, change sets | C++23 standard library | Qt, JUCE, Tracktion |
| `lina_format` | Reading and writing the project document, migrations (doc 16) | `lina_model`, a JSON parser | Qt, JUCE, Tracktion |
| `lina_engine` | Projection into a Tracktion edit, transport and launch control, recording, rendering, plugin hosting, engine-state snapshots | `lina_model`, Tracktion, JUCE | Qt |
| `lina_bridge` | Event-loop integration, QObject controllers, list models over the document | `lina_model`, `lina_engine`'s public headers, Qt | Tracktion or JUCE types in any header QML can see |
| `lina_ui` | QML and custom scene-graph items | `lina_bridge`, `lina_model` (read-only), Qt Quick | Tracktion, JUCE |
| `lina_app` | `main`, command line, start-up | all | |

- The build enforces the "must not use" column: `lina_model` and `lina_format` have no Qt or
  JUCE include paths, so a stray include is a compile error. The Qt Quick spike built its model
  this way without friction.
- `lina_engine`'s public headers expose LINA types only. Tracktion and JUCE types stay in its
  source files.
- The one place where Qt and JUCE meet in a single file is the event-loop integration in
  `lina_bridge`.

## Processes

1. **The application.** One process holds the document, the engine and the interface.
2. **Plugin scanner.** The same executable started with a scanner argument, one plugin at a time,
   so that a plugin crashing while examined is recorded as failed (doc 09). Tracktion provides
   the child-process mechanism; it is off by default and LINA turns it on.
3. Later: plugin host processes, for isolation of running plugins (doc 15).

## Threads

| Thread | Does | Rules |
|---|---|---|
| Main | Qt event loop, which also serves as JUCE's message thread. All document mutation, all Tracktion edit mutation, QML | Nothing here may block for more than a few milliseconds |
| Qt render | Scene-graph rendering. Reads item state during the sync phase while the main thread is blocked | Never touches the engine |
| Audio (several) | Tracktion's node graph: playback, plugins, launch timing | No allocation, locks, I/O or logging (`CLAUDE.md`); never touches the document |
| Workers | Waveform overviews, tempo detection, file copying and hashing, writing saves and recovery copies | Deliver results to the main thread; never mutate the document themselves |
| Recording writer | Writes incoming audio to disk | Tracktion's own |

- **One event loop.** Qt owns it. JUCE's file descriptors are watched with Qt socket notifiers,
  as the first spike did (154 lines, microsecond overhead) and as Zrythm does. This relies on a
  JUCE header marked internal and must be re-checked on every JUCE upgrade.
- **The document is main-thread only.** Custom items read it during the scene-graph sync phase,
  when the main thread is blocked, so no lock is needed. The engine never reads the document; it
  sees only its projection.
- **Known hazard:** code in JUCE or a plugin that runs its own nested event loop (a blocking
  dialog) starves Qt for as long as it is up. Not yet exercised.

## The document model (`lina_model`)

1. **Musical time** is a 64-bit integer count of ticks, 15,120 per quarter note. That number
   divides evenly by 16, 20, 24 and 28, which covers straight, quintuplet, triplet and septuplet
   subdivisions down to 1/64 notes (doc 04). Decided 2026-11-13.
2. **Identities** are 64-bit integers taken from a counter stored in the document, never reused
   and never random, so tests and saves are deterministic (doc 10).
3. **Commands** are plain values (`MoveClip`, `SetProjectProperty` with a tempo, ...). `apply` is the only way to change
   the document. Each command yields its inverse; undo applies inverses. One user gesture is one
   transaction of one or more commands (doc 12). Decided 2026-11-13: undo by inverse commands,
   not by keeping earlier versions of the document. Tests check, for generated command
   sequences, that applying a command and then its inverse restores the document exactly.
4. **Rules of the model are enforced in `apply`:** clips on a track never overlap, same-pitch
   notes never overlap, every track has one slot per scene (docs 03, 06, 08). The engine is
   never relied on for these.
5. **Change sets.** Applying a command returns a description of what changed (which objects,
   which properties). Both the interface and the projection consume it. The Qt Quick spike used
   a single coarse "document changed" signal and rebuilt everything visible; that is the thing
   to do better. The contract a consumer must follow (what a merged change set does and does
   not list) is written out in the header that defines it.
6. **Restoring is internal.** The commands that put a removed object back under its old
   identity exist only as inverses inside the document. No caller can issue one, so an identity
   can never come back as a different kind of object.
7. **A document always passes validation.** The same rule functions serve the commands and the
   whole-document check that the file loader uses, including range checks on every enumeration
   and the rule that every position plus length stays within 2^53 ticks, so that every stored
   time converts to floating point exactly.
8. A refused command leaves the document unchanged and names the object the caller addressed.
   An inverse that is refused is a defect in the library, and the process aborts.

## The projection (`lina_engine`)

1. Holds a map from LINA identities to Tracktion objects and updates the edit incrementally from
   change sets.
2. Converts ticks to the engine's beats and seconds on the way in. Nothing is converted back
   (doc 04).
3. **Test oracle:** an edit built incrementally through any sequence of commands must equal an
   edit built from scratch from the resulting document. This is checked in tests for randomly
   generated command sequences.
4. Tracktion's own undo manager and file saving are not used.
5. Policies the engine leaves to the application live here, on top of its launch handles for
   audio clips; MIDI clips are played and launched by LINA's own sequencer (open question 2): one
   clip per track, scene launch with empty slots stopping, "stop all", the relaunch set, launch
   boundaries computed in ticks, rounding record length to quantisation, return-track and send
   bookkeeping, exclusive solo.
6. **What flows back, and how:**
   - *Engine state* (playhead, slot states, meters, dropout count, total compensated delay) is
     published as snapshots and atomics that the interface polls once per frame.
   - *Engine-produced data* (plugin state, finished recordings, detected tempos) reaches the
     document only as commands issued on the main thread.

## Engine bring-up (`lina_engine`, `lina_bridge`)

Built 2026-11-19, ahead of the projection.

1. **No device, driven by hand.** Tests open a manual device and ask for a number of frames.
   The engine runs Tracktion's playback graph on the calling thread and returns the audio. The
   output is bit-identical across block sizes, across one and several audio threads, and
   between two engines in one process.
2. **An offline render is playback.** A render of a range is defined as exactly what playback
   started at the first frame of that range produces, with plugins freshly initialised and no
   pre-roll; both run the same code. Tracktion's own renderer is not used: it sleeps on the wall
   clock, and its pre-roll makes a partial render differ from playback. A render started in the
   middle of a project is therefore not a slice of a render of the whole.
3. **Files are replaced safely.** A render to a file is written under a temporary name next to
   the target, checked, and renamed over it. A failure leaves an existing file untouched.
4. **Changing the edit while playing.** After the edit is changed, one call rebuilds the
   playback graph at once and playback continues; sounding notes and plugin state carry across.
   Tracktion's own route for this runs off a timer and is not used. Edits of MIDI notes and
   clips and of the mixer rebuild nothing ("The projection as built", 1 and 3).
5. **Errors.** Mistakes by the caller (wrong thread, no device, bad format or range) throw.
   Failures of the outside world (files) are returned as values.
6. **Event loop.** The bridge registers JUCE's file descriptors with Qt's event loop, so one
   loop serves both. File descriptors registered from other threads are tolerated, not
   supported.
7. **Known departures from the docs.** The own render path and the wait below were accepted
   on 2026-11-21.
   - Creating Tracktion's playback context waits about 0.2 seconds of wall-clock time when the
     device is driven by hand. It is created once per engine, so each engine test pays it once.
     Tracktion has a similar wait of up to 0.1 seconds when launched clips are stopped and when
     a recording is prepared.
   - Edits built by hand in tests, without the projection, still get Tracktion's own note
     timing: every note ends 0.0001 seconds early.

## The projection as built

Built 2026-11-22. Scope so far: tempo, tracks, arrangement clips (MIDI clips played by LINA's
own sequencer, audio clips at file speed), volume, pan, mute, solo, sends and returns, and
devices through a registry that plugin hosting will fill. The time signature is not projected:
nothing in the engine needs it, so Tracktion's stays at 4/4 and hosted plugins that read the
host's bar position see 4/4. Not projected yet: session slots and launching, automation,
controller data, the loop range and start marker, arm, monitoring and inputs, follow-tempo and
transposition, edge fades and loop-join crossfades. Change sets that touch those are ignored
safely.

1. **One entry point.** The application hands the engine a whole project once, and after that
   each change set together with the document after the change. While a device is open the
   engine rebuilds its playback graph itself, at most once per call, and only when the change
   touches what Tracktion plays (audio clips, tracks, devices, sends, tempo); nothing is left
   for Tracktion's timer, also after `play()`. Tracktion asks for one more rebuild whenever a
   new graph registers its live-MIDI node on a track with no listeners; the projection keeps a
   listener of its own that does nothing on every track, so that request never comes. This
   relies on Tracktion's `AudioTrack::addListener` and must be rechecked on any upgrade.
2. **Time.** Ticks become beats, seconds and frames in one place, with 128-bit integer
   arithmetic and rounding to the nearest frame. A beat is always a quarter note.
3. **LINA plays MIDI clips itself.** MIDI clips do not become Tracktion clips. A sequencer
   plugin of LINA's own sits first on every MIDI track. On the main thread each edit builds an
   immutable sequence of the track's note events, computed from the document's ticks, and
   publishes it through one atomic pointer; the audio thread picks it up at the start of its
   next block and marks the one it uses, and the main thread frees every older sequence except
   the marked one, so at most two are kept. Every buffer is owned on the main thread. The audio
   thread does not allocate, lock or free. Notes start and end on the
   exact frame. A note-off is handed over 1/8 of a frame into its frame and a note-on 3/8, so
   that where one clip ends and the next begins on the same pitch the off always sorts before
   the on.
4. **What sounds across starts and edits.** The sequencer keeps its own record of the notes it
   started. When playback starts inside a note, the note starts on the first frame played
   (doc 08, rule 3). After an edit, a jump or a tempo change it ends the notes the new content
   no longer holds under the playhead and starts none (doc 08, rule 9); held notes run on to
   their new end. A block whose messages would not fit the room reserved for it is not sent:
   the instrument gets "all notes off" instead and the block is counted; tests and benches
   check that this never happens. Tracktion's MIDI arrays cannot be reserved in advance, so the
   sequencer swaps buffers of its own, made on the main thread, into them. Memory: about 147 KB
   per MIDI track while it is in a playback graph. It does not yet tell clip notes from live
   notes (no live input exists before recording is built).
5. **Mixer.** Gain is 10^(dB/20), exactly zero at silence. Pan is a constant-power balance with
   unity gain at centre (+3.01 dB hard to one side). Both are applied by a plugin of LINA's
   own with a 5 millisecond ramp counted in frames; Tracktion's volume and pan plugin is not
   used. Sends are after the fader.
6. **Audio files** are mapped synchronously when first used, so a render never holds silence
   for a file that is not ready. Only WAV and AIFF can be played this way; other formats are
   reported and silent until the import package converts them. A file's own sample rate wins
   over the one the document states. The engine reports missing, unreadable, unsupported and
   mismatched files, and clips cut short by the limit of 250,000 notes after loop unrolling.
7. **Failure.** If an update fails part-way, the edit is rebuilt from the project and the
   error is raised again. If the rebuild fails too, no project is loaded.
8. **Limits of the engine.**
   - Tracktion's timeline ends at 48 hours. Clips that start at or behind 47.5 hours do not
     play. The document allows far more.
   - Every edit that changes Tracktion's graph makes it rebuild the whole graph. Measured in an
     optimised build on doc 11's large project (256 tracks, about 519,000 notes), main thread,
     medians: a note or MIDI clip edit 0.03 to 0.14 ms (worst 0.18 ms), a fader or mute
     0.01 ms, the time signature nothing, all without a rebuild; an audio clip move 66 ms and
     a tempo change 125 ms, each with one rebuild. Before LINA played MIDI itself, every note edit cost about
     340 ms.
   - With a real device, Tracktion can hand a rebuilt graph to the audio thread a block before
     the new MIDI sequences are published. After a tempo change one block can then play new
     audio against old MIDI; the sequencer reconciles on the next block and no note sticks.
   - A file at another sample rate than the device is converted at play time with the
     engine's best converter: residual about -146 dB, but the start is shifted by up to half
     a frame depending on block size.
9. **Test oracle.** Four kinds of generated sequences: structure compared as text after every
   step; rendered audio compared bit for bit; one case with mismatched sample rates; and edits
   applied while playing. Both sides of the oracle share the projection's code, so it cannot
   see a wrong idea they share; sweeps against formulas written in the tests cover that.

## The interface (`lina_ui`, `lina_bridge`)

1. Heavy views are custom scene-graph items that hold retained geometry and move it with
   transforms. Measured in the Qt Quick spike with 10,000 clips on screen: 0.14 ms per frame
   when only the playhead moves, about 7.6 ms on the worst frames (zooming with everything
   visible).
2. Content is built in tiles per track and time range. The spike built one large block and
   measured a 24.7 ms hitch when scrolling out of it.
3. One QML item per document object is forbidden. The spike measured over 100 ms per zoom frame
   that way.
4. Waveforms are built from cached peak data and their geometry is kept between frames.
5. Meters and the playhead are drawn from polled engine state, never through item models or
   per-buffer signals.
6. List models for chrome (track headers, device chains, browser) are thin adapters over the
   document and its change sets.

## Start-up and shutdown

1. Start the Qt application, create JUCE on the main thread, install the event-loop bridge,
   create the engine, then load QML. Plugin scanning starts in the background afterwards.
2. Shutdown stops the audio device, closes plugin windows, destroys the engine, then Qt.

## Open questions

1. How a recovery copy is taken without stalling the main thread on a large project.
2. Editing while playing misses doc 11's response targets on a large project, because
   Tracktion rebuilds its whole graph per edit ("The projection as built", 8). Decided
   2026-11-22: LINA plays MIDI clips itself, through a sequencer of its own on every MIDI
   track, and Tracktion keeps audio clips, the mixer and plugins. Built the same day ("The
   projection as built", 3, 4 and 8): a note or MIDI clip edit costs under 0.2 ms and rebuilds
   nothing. Audio clip edits (about 66 ms) and tempo changes (about 125 ms) still rebuild
   Tracktion's graph at that size; LINA playing audio clips too is the way out if that ever
   matters. Consequence: MIDI clips are launched by LINA's sequencer, not through
   Tracktion's launch handles (item 5 of "The projection" holds for audio clips only).
3. The model finds objects by scanning and has no index by identity. Its cost on the large
   project of doc 11 is unmeasured.
4. Undo history has no depth limit, and each removal keeps a full copy of what it removed.
