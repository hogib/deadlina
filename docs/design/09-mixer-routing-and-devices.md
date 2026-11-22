# 09 Mixer, routing and devices

Status: **draft**. Terms are from doc 03, source tags from doc 01. **(R1)** marks what the first
release needs. Items marked *proposed* are suggestions waiting for a yes or no. Statements are
`[own design]` unless tagged otherwise and describe mixing as it is commonly done across DAWs.

## Signal flow of a track

```
input or clip playback → clip gain → device chain → volume and pan → output
                                                          └→ sends → return tracks
```

1. **(R1)** The order above is fixed. Sends are taken after the track's volume and pan.
2. **(R1)** Every signal path is stereo. A mono file or input is played equally on both sides.
3. **(R1)** Signals between devices and tracks have ample headroom: levels above full scale
   inside the mixer do not distort. Distortion from excessive level can happen only at the final
   output, and the master meter shows when it does.
4. **(R1)** With every volume control at its default and pan centred, a clip plays at the level
   of its file. Nothing in the path changes the sound unless the user asks it to.
5. Later: sends taken before the volume control; more than two channels.

## The mixer strip

1. **(R1)** Volume, from silence to +6 dB, with the default at 0 dB.
2. **(R1)** Pan, with a centre detent. Panning keeps perceived loudness steady across the range:
   pan is a constant-power balance with unity gain at centre, so a signal panned hard to one
   side is 3 dB louder on that side and silent on the other. Decided 2026-11-22.
3. **(R1)** Mute. A muted track keeps processing, so unmuting is instant and reverbs do not
   restart from silence.
4. **(R1)** Solo. Soloing a track silences all tracks that are not soloed. Return tracks stay
   audible so that a soloed track is heard with its send effects. Clicking solo on another track
   moves the solo there; a modifier adds to the soloed set. *Proposed.* Soloing a return track
   silences the other returns and the direct output of tracks that are not soloed, while their
   sends keep feeding the soloed return; soloing a track never silences returns. Decided
   2026-11-22.
5. **(R1)** Arm for recording, with the input chosen on the strip.
6. **(R1)** A level meter showing peak level, with an indicator that latches when the level has
   exceeded full scale until the user clears it.
7. **(R1)** One send level per return track.
8. **(R1)** All of these are parameters: they can be automated (doc 06) and every change is
   undoable. Mute, solo and arm are not automated in the first release.

## Return tracks and the master

1. **(R1)** A project can have any number of return tracks. Adding one adds a send to every
   track.
2. **(R1)** A return track has a device chain and a mixer strip but no clips.
3. **(R1)** A return track cannot send to itself. Sends from one return to another are off in the
   first release, which rules out feedback loops.
4. **(R1)** The master track has a device chain and a volume control. Everything that is heard
   and everything that is exported passes through it.

## Inputs, outputs and monitoring

1. **(R1)** An audio track's input is one of the audio interface's inputs, as a mono channel or
   a stereo pair, or none.
2. **(R1)** A MIDI track's input is all connected MIDI devices, or one chosen device.
3. **(R1)** The computer keyboard can act as a MIDI input for playing notes, so that someone with
   only a laptop can play an instrument. *Proposed.*
4. **(R1)** Every track's output goes to the master.
5. **(R1)** Monitoring, per track: off, on, or automatic. Automatic means the input is heard
   while the track is armed, and gives way to clip playback when a clip plays on that track.
6. Later: routing a track's output into another track; side-chain inputs for devices; recording
   another track's or the master's output; sending tracks to separate hardware outputs; a
   separate cue/headphone output.

## Devices

1. **(R1)** In the first release every device is a hosted third-party plugin (doc 02).
2. **(R1)** A MIDI track's chain is one instrument followed by any number of audio effects. An
   audio track's chain is any number of audio effects.
3. **(R1)** A MIDI track with no instrument makes no sound, and the interface says so on the
   track instead of leaving the user to work it out.
4. **(R1)** Devices can be added, removed, reordered, duplicated, and moved or copied to another
   track, while the project is playing.
5. **(R1)** Each device has a bypass switch. A bypassed device passes signal through unchanged
   and without a click.
6. **(R1)** A plugin's own editor window can be opened and closed. A plugin without an editor
   gets a generic panel listing its parameters.
7. **(R1)** A plugin's parameters are available for automation (doc 06).
8. **(R1)** A plugin's complete state is saved with the project and restored on load (doc 10).
9. **(R1)** If a plugin used by a project is not installed, the project still opens. The device
   is shown as missing, passes audio through unchanged, and keeps its saved state so that saving
   the project again loses nothing.
10. **(R1)** Looking for and examining installed plugins is isolated from LINA: a plugin that
    crashes while being examined cannot take LINA down, and is listed as failed.
11. **(R1)** A plugin that crashes while running in a project can take LINA down with it. The
    loss is then limited to what the recovery copy does not hold (doc 10). Decided 2026-11-13 as
    the first-release position; the earlier draft required that a plugin crash cost no unsaved
    work at all.
12. Later: running plugins isolated from LINA, so that a crashing or hanging plugin costs
    nothing but that plugin.
13. Later: built-in devices under their own silly names (doc 01); note effects before the
    instrument; device groups with parallel chains; saving and loading device presets.

## Delay compensation

1. **(R1)** Some devices delay the signal that passes through them and report by how much. LINA
   compensates across the whole mixer so that all tracks, including those going through return
   tracks, stay aligned at the master.
2. **(R1)** Automation and the metronome are aligned with the compensated audio.
3. **(R1)** When a device's reported delay changes, compensation is updated without the user
   having to do anything.
4. **(R1)** The total delay being compensated is shown somewhere the user can find it, because
   it is felt as lag when playing an instrument live through a heavily delayed project.
5. Later: an option to reduce compensation on the track being played live.

## Open questions

1. Solo behaviour as proposed (one track at a time by default, returns stay audible): yes or no?
2. Computer keyboard as a MIDI input in the first release: yes (proposed) or later?
3. Group tracks (also asked in doc 06): first release or later?
4. Should the first release offer any way to record what the master outputs into a new clip?
   It is the simple way to "bounce" something before track freezing exists.
5. Decided 2026-11-21, to be moved into the rules above: value ranges are volume from
   silence (stored as -96 dB) to +6 dB; pan -1 to 1; send level from silence to 0 dB; plugin
   parameter values normalised 0 to 1. Mute and solo are allowed on return tracks.
6. A track's input is stored without depending on a device being present: an audio input by
   channel number, mono or stereo; a MIDI input as "all" or one device by the name the system
   reports. A device that is missing is kept as stored. Decided 2026-11-21.
