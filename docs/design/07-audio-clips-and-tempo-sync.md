# 07 Audio clips and tempo sync

Status: **draft**. Terms are from doc 03, timing rules from doc 04, source tags from doc 01.
**(R1)** marks what the first release needs. Items marked *proposed* are suggestions waiting for a
yes or no. Statements are `[own design]` unless tagged otherwise.

The project owner decided on 2026-11-13 that the first release needs tempo sync only; editable
warp markers come later.

## What an audio clip is

1. **(R1)** An audio clip plays a region of one audio file: a media reference plus a start and
   end position inside the file, stored in the file's own samples (doc 04).
2. **(R1)** Nothing a clip does changes the file. Two clips may use the same file with different
   regions and settings.
3. **(R1)** A clip has a gain setting, applied before the track's device chain.
4. **(R1)** If the file's sample rate differs from the project's, it is converted to the
   project's rate when it is imported, and the converted copy is what the project uses. The user
   does not have to convert files by hand. Converting once, rather than while playing, keeps
   playback exactly repeatable whatever the audio buffer size (doc 11). Decided 2026-11-22.
   `[own design]` Until import exists, such files are converted while playing; their start can
   then shift by up to half a sample depending on the buffer size.

## Following the tempo

1. **(R1)** Every audio clip has a "follow tempo" switch.
2. **(R1)** With the switch on, the clip's material is tied to musical time: a loop that is four
   bars long stays four bars long at any project tempo. LINA changes the playback speed by the
   ratio of project tempo to the clip's source tempo.
3. **(R1)** With the switch off, the clip plays at its original speed. Its length in seconds is
   fixed, so its length in bars changes with the tempo and it will not stay aligned to the bar
   grid. This is the right setting for speech, sound effects and one-shot hits.
4. **(R1)** Default for a newly created clip: on for files that look like loops (see "Source
   tempo"), off for very short files and for files longer than a threshold. *Proposed*; the
   thresholds are a detail to tune.

## Source tempo

A clip can only follow the tempo if LINA knows how its audio relates to beats. In the first
release that knowledge is a single number per clip.

1. **(R1)** The user can state the source tempo in either of two equivalent ways: as beats per
   minute, or as "this region is N bars/beats long". Changing one updates the other.
2. **(R1)** Tempo detection is part of the first release (decided 2026-11-13). When a clip is
   created, LINA fills in the source tempo from, in order: tempo information stored in the file,
   if any; otherwise an estimate from analysing the audio; otherwise, when the analysis finds no
   steady pulse, the tempo closest to the project tempo at which the region would be a whole
   number of bars.
3. **(R1)** Analysis runs in the background and its result is kept with the file's other cached
   data. A clip is playable before analysis finishes; if the estimate arrives while the clip is
   already in use, it is applied only if the user has not set the tempo by hand.
4. **(R1)** The value is always shown, always editable, and the interface says where it came from
   (file, detected, guessed, or set by hand). Detection is an estimate and will sometimes be
   wrong.
5. **(R1)** Halve and double buttons correct the most common wrong estimate.
6. Later: detecting the position of individual beats, which editable warp markers need.

## How the speed change is done

1. **(R1)** Two modes per clip:
   - **Stretch:** the speed changes and the pitch stays the same.
   - **Repitch:** the speed changes by playing the audio faster or slower, so the pitch moves
     with it, as with a tape or a turntable. No stretching artefacts.
2. **(R1)** When the project tempo equals the source tempo and no transposition is set, a clip in
   stretch mode plays its samples unprocessed.
3. **(R1)** Changing the project tempo while a clip plays changes its speed smoothly, without
   clicks or jumps in position (doc 04).
4. **(R1)** Any delay the stretching process introduces is compensated, so a stretched clip
   starts on its launch boundary like any other (docs 05 and 09).
5. **(R1)** Transposition in semitones and cents, independent of tempo, for clips in stretch
   mode. *Proposed* for the first release because it comes at little extra cost once stretching
   exists; see open question 2.
6. Later: stretch modes tuned for particular material (percussive, tonal, full mixes) and
   preservation of vocal character when transposing.

## Looping and playback range

1. **(R1)** The clip's start position, loop switch, loop start and loop length (doc 05) are in
   always stored in musical time, like every position in a project (doc 04). With "follow
   tempo" off the interface may show them in seconds. Decided 2026-11-21.
2. **(R1)** A loop repeats without a click at the join. Where the audio itself does not join
   cleanly, a very short crossfade is applied at the loop point.

## Forward compatibility with warp markers

1. **(R1)** Internally, the relation between file positions and musical time is stored as a list
   of warp markers from the start. A first-release clip simply has two: one at the region start
   and one at the region end. The single "source tempo" number is a view of those two markers.
2. Consequence: when editable warp markers arrive, existing projects need no conversion, and the
   file format (doc 10) does not change shape.

## Importing and displaying audio

1. **(R1)** Import formats: uncompressed WAV and AIFF, and FLAC. *Proposed* also for the first
   release: MP3 and Ogg Vorbis/Opus, since people's sample folders are full of them.
2. **(R1)** A waveform overview is computed in the background when a file is first used and kept
   for next time. A clip is playable before its overview is ready.
3. **(R1)** A file that is missing when a project is opened is reported clearly; the clip keeps
   its settings and plays silence until the file is found or relinked (doc 10).

## Later

- Editable warp markers and audio-to-grid correction.
- Finding note and drum onsets in audio.
- Reversing a clip; slicing a clip into a MIDI-triggered kit.

## Open questions

1. Transposition in the first release: yes (proposed) or later?
2. Which compressed formats must import in the first release?
3. Tempo detection estimates a tempo; it does not find where bar one starts. Is it enough for the
   first release that the user trims the clip start to the downbeat by hand?
4. As built 2026-11-22: clip edges and loop joins are hard cuts; the fades of doc 06 and the
   loop-join crossfade above are not built yet. Formats other than WAV and AIFF do not play
   until import converts them.
