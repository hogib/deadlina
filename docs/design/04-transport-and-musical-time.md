# 04 Transport and musical time

Status: **draft**. Terms are from doc 03. Source tags follow doc 01. Almost everything here is
`[own design]` built on common DAW practice; the few statements about how Live behaves are tagged
separately. **(R1)** marks what the first release needs; everything else may come later.

## Musical time

1. **(R1)** Positions and lengths of clips, notes, automation points, warp markers and loop
   ranges are stored in musical time, not in seconds or samples. `[own design]`
2. **(R1)** Musical time is stored exactly, as whole numbers of a fixed subdivision of a beat.
   The subdivision must divide evenly by the note values users can pick from, including triplets
   and quintuplets down to 1/64 notes. No stored position is a floating-point number.
   `[own design]`
3. **(R1)** A "beat" in stored time is always a quarter note, whatever the time signature. Bars
   and beats as shown to the user are derived from the time signature map. `[own design]`
4. **(R1)** Exactness is a property of the document: what is stored, edited, saved and compared
   is exact. Playback positions are derived from the document through the tempo map and the
   sample rate and are placed to the nearest sample. The same build of LINA gives the same sample
   positions for the same project on every run; identical positions across different builds are
   a goal, not a guarantee. Narrowed on 2026-11-13 after the engine fit check, when the owner
   decided that LINA owns the document and the playback engine works from a projection of it.
   `[own design]`
5. **(R1)** The playback engine never writes positions back into the document. No stored value
   has ever been through a conversion to seconds and back. `[own design]`
6. Sample-accurate items that are naturally in audio time (a position inside an audio file) are
   stored in samples of that file, with the file's sample rate. `[own design]`

## Tempo and time signature

1. **(R1)** A project has one tempo at any moment. The first release needs only a single
   project-wide tempo that the user can change while playing. `[own design]`
2. **(R1)** Tempo range: 20 to 300 beats per minute, with a resolution of 0.01. Decided
   2026-11-13; the upper limit is the one the playback engine supports without modification.
   `[own design]`
3. **(R1)** A project has one time signature at any moment; the first release needs only a single
   project-wide signature. Numerator 1 to 99, denominator 1, 2, 4, 8 or 16. *Proposed.*
   `[own design]`
4. Later: a tempo map (tempo changes placed on the arrangement) and a time signature map (changes
   at bar lines only). `[own design]`
5. Later: a scene may carry a tempo and a time signature that are applied when the scene is
   launched. `[public: unverified]` that Live offers this; the behaviour here would be specified
   in doc 05.
6. **(R1)** Tap tempo: the user taps a key or button and the tempo follows the average interval
   of the last taps. `[own design]`

## Transport states

1. **(R1)** The transport is either stopped or playing. Recording is not a third state: it is a
   property that can be on while playing (see docs 05 and 06). `[own design]`
2. **(R1)** Play starts from the start marker, a position the user sets by clicking in the
   arrangement. Stop halts playback and leaves the playhead where it stopped; a second stop returns
   the playhead to the start marker. Decided 2026-11-13. `[own design]`
3. **(R1)** "Continue" starts from where the playhead stopped. `[own design]`
4. **(R1)** While playing, the playhead position is reported to the interface often enough for
   smooth motion, and that reporting never delays audio. `[own design]`
5. **(R1)** Launching a clip or scene while stopped starts the transport. `[own design]`

## Loop range

1. **(R1)** The arrangement has one loop range with a start and a length in musical time, and a
   switch. When the switch is on and the playhead reaches the end of the range, it jumps to the
   start without a gap or a click. `[own design]`
2. **(R1)** Clips in the session are not affected by the arrangement loop range; they loop by their own
   settings (doc 05). `[own design]`
3. Later: punch-in and punch-out points for recording into a range. `[own design]`

## Metronome

1. **(R1)** A metronome that can be switched on and off while playing, with an accent on the
   first beat of each bar. It is routed so that it is heard but never ends up in an export.
   `[own design]`
2. **(R1)** Count-in before recording: none, 1 bar, 2 bars or 4 bars. `[own design]`

## Quantisation grids

Two different things are called quantisation. This doc names them apart.

1. **(R1) Launch quantisation** decides when a launched clip or scene actually starts. Values:
   none, 8 bars, 4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/8, 1/16, 1/32. The project has a default; a
   clip may override it. *Proposed* value list. `[own design]`
2. **(R1)** Launch boundaries are counted from the start of the arrangement, on the bar grid given
   by the time signature. With launch quantisation of 1 bar, a launch requested during bar 7
   takes effect at the start of bar 8. `[own design]`
3. **(R1)** A launch requested exactly on a boundary takes effect on that boundary, not the
   next. `[own design]`
4. **(R1) Edit snapping** decides where dragged or drawn items land in an editor. It is a
   property of each editor view, follows the zoom level by default, and can be fixed to a value
   or switched off. It never changes what launch quantisation does. `[own design]`
5. **(R1) Note quantisation**: a command that moves the start of each selected note to the
   nearest line of a chosen grid. Decided 2026-11-13 for the first release in this basic form
   (doc 08). `[own design]`
6. Later: quantising by a chosen strength, quantising note ends and warp markers, and
   swing/groove templates. `[own design]`

## Tempo changes while playing

1. **(R1)** Changing the tempo while playing takes effect immediately and without a gap. MIDI
   clips follow because their content is in musical time. The playhead keeps its musical
   position; a note sounding at the change runs on to its end at the new tempo, and no note
   restarts (doc 08, rule 9). `[own design]`
2. **(R1)** Audio clips set to follow the tempo are time-stretched to stay in musical time; audio
   clips not set to follow it keep their original speed and drift against the grid (doc 07).
   `[own design]`

## Open questions

1. Should gradual tempo changes (ramps) exist when the tempo map arrives, or only steps?
2. Should launch quantisation "none" be offered at all in the first release? It is the setting
   most likely to confuse someone new to clip launching.
