# 08 MIDI clips and note editing

Status: **draft**. Terms are from doc 03, timing rules from doc 04, source tags from doc 01.
**(R1)** marks what the first release needs. Items marked *proposed* are suggestions waiting for a
yes or no. Statements are `[own design]` unless tagged otherwise; the MIDI value ranges are from
the MIDI 1.0 specification `[literature: MIDI 1.0 Detailed Specification]`.

## The note model

1. **(R1)** A note has a pitch (0 to 127), a start and a length in musical time, and a velocity
   (1 to 127).
2. **(R1)** A note has a stable identity, so that selection, undo and later per-note data refer
   to the note itself and not to its position.
3. **(R1)** Notes do not carry a MIDI channel. The track decides what its instrument receives.
   *Proposed.*
4. **(R1)** Two notes of the same pitch never overlap in one clip. An edit that would create an
   overlap shortens the earlier note so that it ends where the later one starts. *Proposed.*
5. **(R1)** A note's length is at least one stored time unit; a zero-length note cannot exist.
6. Later: release velocity, a mute flag per note, per-note chance of playing, and per-note pitch
   and pressure curves for expressive controllers.

## Controller data

1. **(R1)** Pitch bend, the sustain pedal, the modulation wheel, other controllers and channel
   pressure that arrive while recording are stored in the clip and played back with it. A
   keyboard performance is not flattened to bare notes. *Proposed.*
2. **(R1)** Each kind of controller data is stored as its own envelope inside the clip, in
   musical time.
3. Later: viewing and editing those envelopes.

## Playback

1. **(R1)** When a clip stops for any reason (stopped, replaced by another clip, transport
   stopped, track deleted, loop wrapping around), every note it has started is ended. A stuck
   note is a defect of the highest severity.
2. **(R1)** A note that starts inside the playing range but extends beyond the loop end or clip
   end sounds until that boundary and is cut there. The stored note keeps its full length.
3. **(R1)** When playback begins in the middle of a note, the note sounds from that point for
   the rest of its length, so that a long held note or pad is not silent just because play
   started after its beginning. Decided 2026-11-13. It starts on the exact first frame played.
   Decided 2026-11-22.
4. **(R1)** When a clip stops, held sustain pedal and pitch bend are returned to rest so that the
   next clip does not inherit them.
5. **(R1)** A note that starts before the clip's content start, or before the loop start once the
   loop has wrapped, does not sound, even where its end lies in the played part. Rule 3 applies
   only to notes the clip would have played had playback begun at its content start. Decided
   2026-11-22 to follow Live. `[public: unverified]` (user forum reports, not the manual)
6. **(R1)** Splitting a clip does not split the notes that cross the cut: in the second clip such
   a note starts before the content start and does not sound (rule 5). Splitting notes is a
   separate command of the note editor. Decided 2026-11-22 to follow Live; Live's manual
   describes a separate note-splitting command, and user reports describe the clip split.
   `[manual]` for the separate command, `[public: unverified]` for the clip split
7. **(R1)** Notes carry no MIDI channel. Every note is sent to the track's instrument on channel 1.
   Decided 2026-11-22 for the first release; multi-channel setups need another mechanism later.
8. **(R1)** An edit made while playing does not start a note that already covers the playhead:
   a note drawn under the playhead, or a clip moved so that it covers the playhead, sounds from
   the note's next start. Rule 3 still applies when playback itself begins in the middle of a
   note. Dragging a clip across the playhead makes many small moves, and starting notes on
   each would make blips. Decided 2026-11-22. `[own design]`
5. **(R1)** Note timing is sample-accurate within the audio buffer; notes are not rounded to
   buffer boundaries.

## The note editor (piano roll)

1. **(R1)** Pitches run vertically with a piano keyboard at the left; time runs horizontally with
   the same ruler and snapping rules as the arrangement (doc 04).
2. **(R1)** Operations: draw a note, select (click, rubber band, extend with a modifier, select
   all), move in time and pitch, change length from either end, delete, duplicate, copy and
   paste.
3. **(R1)** Keyboard operations on the selection: move by one snap step, transpose by a semitone
   and by an octave.
4. **(R1)** A velocity lane under the notes shows one bar per note; dragging changes velocity for
   the note or for the whole selection.
5. **(R1)** Clicking or moving a note plays it through the track's instrument, so the user hears
   what they are editing. This can be switched off.
6. **(R1)** The clip's start, loop range and length are shown and editable in the same view.
7. **(R1)** Editing works while the clip is playing, and changes are heard on the next pass
   without stopping playback.
8. **(R1)** Every edit is undoable. A drag is one undo step, not one per pointer movement.
9. Later: showing only the pitches in use; highlighting a chosen scale; a drum layout with one
   named row per sound; entering notes step by step from a keyboard.

## Recording

1. **(R1)** Notes are recorded exactly as played, without correction to the grid.
2. **(R1)** Recorded note times are corrected for the measured input delay, so notes land where
   the musician played them relative to what they heard (doc 11).
3. **(R1)** Notes held when recording ends are ended at the end of the recording.
4. Later: an option to correct timing to the grid while recording.

## Correcting timing

1. **(R1)** A "quantise" command moves the start of every selected note (or of every note in the
   clip when nothing is selected) to the nearest line of a chosen grid value. Note lengths are
   kept; rule 4 of the note model resolves any overlaps that result. Decided 2026-11-13.
2. **(R1)** The grid value defaults to the editor's current snap value and can be chosen in the
   command.
3. Later: partial strength, quantising note ends, swing.

## Files

1. Later: importing a standard MIDI file as one or more clips, and exporting a clip as one.
   See open question 1.

## Open questions

1. Should standard MIDI file import be in the first release? It is the only way to bring in
   existing note material.
2. Live lets the user turn chasing of notes (rule 3) off. Should LINA offer that switch? `[manual]`
3. Rules 5 and 6 rest on user reports of Live's behaviour; check them against Live itself if the
   owner has a copy (doc 01, open question 1 on observation).
