# 12 Interaction principles

Status: **draft**. These are the rules every part of the interface follows. They say nothing about
what LINA looks like; the visual design is original work done separately (doc 01). The audience
is Linux musicians who have not used Live (doc 02), so nothing here assumes habits from it.
**(R1)** marks what the first release needs. Items marked *proposed* are suggestions waiting for a
yes or no. All statements are `[own design]`.

## Principles

### 1. Show state; do not make the user remember it

- **(R1)** Anything that changes what the user will hear is visible where they are looking: a
  clip waiting to launch, a track suspended by the session (doc 05), an automated parameter that
  has been overridden by hand (doc 06), an armed track, a muted or soloed track, a missing file
  or plugin.
- **(R1)** When the user hears silence, the interface can always answer "why?".

### 2. Nothing stops the music

- **(R1)** Every edit can be made while playing, and playing never has to stop for the interface.
- **(R1)** No routine action opens a dialog that blocks the rest of the window.

### 3. Everything can be undone

- **(R1)** Every change to the project is undoable, including mixer moves and device changes.
  One exception (decided 2026-11-13): changes made inside a third-party plugin's own window are
  not individually undoable in the first release (doc 15).
- **(R1)** One gesture is one undo step. Dragging a fader or a clip is a single step, however far
  it moved.
- **(R1)** Undo does not touch things that are not part of the project's content: selection,
  zoom, scroll position, transport position, which clips are playing.
- **(R1)** Undo and redo name what they will do ("Undo move clip").
- **(R1)** Because edits are undoable, LINA does not ask "are you sure?" for them. Confirmation
  is reserved for what cannot be undone, such as deleting files from disk.

### 4. The keyboard reaches everything

- **(R1)** Every command has a name and can be run from a searchable command list opened with one
  key. Each entry shows its shortcut, so the list is also how shortcuts are learned. *Proposed.*
- **(R1)** Shortcuts follow Linux desktop conventions where one exists (undo, redo, save, copy,
  paste, select all, quit) and the Space bar starts and stops the transport.
- **(R1)** Shortcuts are chosen for LINA on their own merits; matching another DAW's shortcut map
  is not a goal.
- Later: user-defined shortcuts.

### 5. One place has the keys

- **(R1)** Exactly one pane has keyboard focus, and it is visibly marked. Keys such as Delete,
  arrows and select-all act on that pane.
- **(R1)** Transport keys work whichever pane has focus, except while typing into a text field.
- **(R1)** Finishing or cancelling a text entry returns focus to where it was.

### 6. The same gesture means the same thing everywhere

- **(R1)** Selecting, extending a selection, rubber-band selecting, duplicating by dragging with a
  modifier, bypassing snap with a modifier, and cancelling a drag with Escape work identically
  in the session, the arrangement, the note editor and automation lanes.
- **(R1)** Double-click opens or creates; it never deletes.

### 7. Things can be dragged to where they belong

- **(R1)** Clips, devices and files can be dragged, and while dragging, the places that accept
  the drop are highlighted and the places that do not are plainly inert.
- **(R1)** Files can be dragged in from the desktop's file manager.
- **(R1)** Everything that can be done by dragging can also be done by a command, for keyboard
  users (principle 4).

### 8. Controls take values directly

- **(R1)** Every numeric control can be dragged, adjusted finely with a modifier, set by typing a
  value with its unit, and reset to its default by double-clicking.
- **(R1)** Values are shown in the units musicians use (dB, bars and beats, semitones, Hz,
  milliseconds), never as bare internal numbers.

### 9. The interface explains itself

- **(R1)** An empty area says what it is for and how to fill it: an empty session invites a
  sample to be dropped, a MIDI track without an instrument says it needs one.
- **(R1)** Pointing at any control shows its name and a one-sentence explanation in a fixed help
  area. *Proposed.*
- **(R1)** Interface text uses the terms of doc 03 and no others.
- **(R1)** Error messages say what happened and what the user can do next, in plain language.

### 10. One window

- **(R1)** LINA is one main window. Plugin editors are separate windows that stay associated
  with it.
- Later: a second main window so that two views can be seen on two screens.

## Layout

1. **(R1)** The session and the arrangement can be visible together in the main window, so that
   clips can be dragged directly from one to the other. Decided 2026-11-13.
2. **(R1)** Either view can also be shown alone, using the whole area, for small screens and for
   focused work. *Proposed.*
3. **(R1)** Both views show the same tracks in the same order, and a track selected in one is
   selected in the other.
4. **(R1)** When both are visible they are stacked, one above the other, with a divider the user
   can drag. Decided 2026-11-13. The session keeps tracks as columns and the arrangement keeps
   them as rows.
5. Where the clip editor, the device chain and the mixer sit is open question 1.

## Open questions

1. With the two views stacked, which is on top, and where do the clip editor, the device chain
   and the mixer go? A stacked pair already uses most of the window's height.
2. The searchable command list in the first release: yes (proposed) or later?
3. The fixed help area that explains whatever is under the pointer: yes (proposed) or later?
4. The shell built on 2026-11-23 made provisional choices, to be confirmed when the session and
   arrangement views are built (the owner deferred them then):
   - The session is on top. The divider's position is not saved, because the view file has no
     field for it; the pane that has the keys is saved.
   - The command list (question 2) and the help area (question 3) are built as proposed. The
     command list opens with Ctrl+K.
   - Shortcuts: Space plays from the start marker or stops, Shift+Space continues from the
     playhead, a second stop returns to the start marker (the stop button names what it will
     do), F6 and Shift+F6 move between panes, Ctrl+1 and Ctrl+2 go to the session and the
     arrangement, Ctrl+T and Ctrl+Shift+T add an audio and a MIDI track, Ctrl+= or Ctrl++,
     Ctrl+- and Ctrl+0 change the size, Escape cancels a drag, Ctrl+Y also redoes; Ctrl+N, O,
     S, Shift+S, Q, Z and Shift+Z as usual.
   - Command names are in title case ("Undo Add Track").
   - The pane with the keys has a thicker frame as well as the focus colour, so focus is not
     shown by colour alone. Clicking a button does not take the keys from the pane.
