# 16 Project file format: encoding

Status: **draft**. Stack-dependent. Requirements come from doc 10; this doc says how they are
encoded on disk. It describes format version 1 **as built** in `lina_format`. Version 1 is not
released: until it is, anything here can still change without a migration. Choices the owner has
not confirmed are listed under "Open questions" as "as built 2026-11, to confirm".

## Project folder

```
My Song/
├── project.lina            the document (text)
├── view.lina               zoom, scroll, which view is showing (text)
├── media/                  imported copies and recordings
│   └── kick-3f9a1c2e.wav
├── state/                  plugin state, one file per distinct state
│   └── 8d41…e07.bin
├── backup/
│   ├── project.lina        the previous saved document
│   ├── project.v1.lina     kept apart: the last document of an older format version
│   └── project.unreadable.lina   kept apart: a document whose version could not be made out
├── recovery/               present only while there are unsaved changes
│   └── project.lina
├── cache/                  waveform overviews, detected tempos; safe to delete
└── .lock                   present while the project is open
```

- View state is a separate file so that scrolling and zooming do not change the document and do
  not show up when the document is compared between versions.
- Files inside the folder that LINA reads or replaces must be regular files. A symbolic link in
  the place of `project.lina`, `view.lina`, a backup, the recovery copy, a state file or the lock
  file is refused with an error; it is never followed and never silently replaced. Media files may
  be symbolic links, and so may the source of an import.
- Temporary files are named after their target with `.tmp` appended (`project.lina.tmp`,
  `<name>.import.tmp` in `media/`). They are never read. An interrupted run can leave one behind;
  they are swept away once at open, after the lock is taken, and only names LINA itself creates
  are touched.

## Encoding of the document

1. **JSON, UTF-8, Unix line endings**, written by LINA's own writer in a fixed canonical layout:
   - two-space indentation, a newline at the end of the file, no byte order mark;
   - an object or list either puts each member on a line of its own, or, for small records
     (a scene, a note, a point, a warp marker, a send, a parameter value, a loop, a region, an
     input, an automation target, the time signature), stays on one line as
     `{ "id": 4, "name": "Intro" }` or `[7, 8]`. Lists of such records have one record per line;
   - empty lists and objects are `[]` and `{}`;
   - object keys in the order given below, never alphabetical;
   - every list in the order the model keeps it. The writer never sorts, and the reader refuses a
     list that is out of the model's order (notes by start, then pitch; arrangement clips by
     position; envelopes by identity; device parameters by identifier; points by time).
2. **Numbers.**
   - Musical time (ticks), sample positions, identities, sizes and counts are integer literals,
     exact over the whole 64-bit range.
   - Other values (gain, pan, levels, parameter values) are written as the shortest decimal that
     reads back to the same value, always with a fraction or an exponent (`0.0`, `-6.5`, `1e-06`),
     using the C++ standard library's locale-independent conversions.
   - Tempo is stored by the model in hundredths of a beat per minute and written as the exact
     decimal (`120.0`, `93.47`). It is read only if it is exactly a multiple of 0.01 in range.
   - No value is written or read in a way that depends on the system's language settings.
3. **Strings** are UTF-8 without the character U+0000. The writer escapes `"`, `\` and control
   characters (`\n`, `\t`, `\u001f`, …) and writes everything else as it is.
4. **Enumerations** are written as the names listed below, never as numbers. A name, once
   released, is never reused for something else.
5. **Absent values** are `null`; the key is always there. Variants are objects with a `"type"`
   member that says which shape follows.
6. **Parsing** uses nlohmann/json (MIT licence, header-only, version 3.12 or later), approved as a
   dependency on 2026-11-13 and used for parsing only, through its event interface; the tree the
   migrations and the reader work on is LINA's own, and numbers are converted by LINA from the
   literal text. Writing uses LINA's own canonical writer.

## Schema of version 1

Types: *id* is an integer from 1 up, below `next_id`, used by exactly one object of the document;
*ref* is the id of another object; *ticks* and *samples* are integers; *decimal* is a number;
*colour* is `"#rrggbb"` with lower-case digits.

**Document** (the top-level object), keys in this order:

| Key | Type | Meaning |
|---|---|---|
| `format` | integer | Format version: `1`. |
| `written_by` | string | Name and version of the application that wrote the file. Required; has no effect on loading. |
| `next_id` | integer | The identity counter: 1 to 2^62. |
| `tempo` | decimal | 20 to 300, in steps of 0.01. |
| `time_signature` | `[integer, integer]` | 1 to 99 over 1, 2, 4, 8 or 16. |
| `loop` | `{ enabled, start, length }` | The arrangement's loop range: boolean, ticks, ticks. |
| `start_marker` | ticks | Where play starts from. |
| `launch_quantisation` | name | `none`, `8_bars`, `4_bars`, `2_bars`, `1_bar`, `1/2`, `1/4`, `1/8`, `1/16`, `1/32`. |
| `media` | list of media references | |
| `scenes` | list of scenes | In display order. |
| `tracks` | list of tracks | Audio and MIDI tracks, in display order. |
| `returns` | list of tracks | Return tracks, in display order. |
| `master` | track | |

**Media reference**: `id` (id), `path` (string, see "Media"), `size` (integer, bytes), `hash`
(string: 64 lower-case hexadecimal digits, the SHA-256 of the content), `sample_rate` (integer).

**Scene**: `{ id, name }`.

**Track**, the same shape for every kind: `id`, `kind` (`audio`, `midi`, `return`, `master`),
`name`, `colour`, `mixer`, `devices` (list), `slots` (list; one per scene in scene order on audio
and MIDI tracks, empty otherwise), `arrangement` (list of clips), `automation` (list of
envelopes).

**Mixer**: `volume_db` (decimal), `pan` (decimal, -1 to 1), `mute`, `solo`, `armed` (booleans),
`monitoring` (`off`, `on`, `auto`), `input`, `sends` (list of `{ target, level_db }`, one per
return track in return order; `target` is the ref of the return track).

**Input** (variant): `{ "type": "none" }`, `{ "type": "audio", channel, stereo }` (integer from
0, boolean), `{ "type": "all_midi" }`, `{ "type": "midi_device", device }` (string: the name the
system reports).

**Device**: `id`, `role` (`instrument`, `audio_effect`), `plugin` (`format`, `identifier`,
`name`, `vendor`, `version`: strings), `state` (`null` for no stored state, or the name of a
state file: 64 lower-case hexadecimal digits and `.bin`), `bypassed` (boolean), `parameters`
(list of `{ id, value }`: the plugin's own parameter identifier and a decimal from 0 to 1).

**Slot**: `id`, `scene` (ref), `clip` (`null` for an empty slot, or a clip).

**Clip**: `id`, `name`, `colour`, `position` (ticks; 0 for a clip in a slot), `length` (ticks),
`content_start` (ticks), `loop` (`{ enabled, start, length }`), `launch_quantisation` (`null`
means "use the project's"; a name, `none` included, overrides it), `content`.

**Content** (variant):
- `"type": "midi"`, then `notes` (list of `{ id, start, length, pitch, velocity }`) and
  `controllers` (list of `kind`, `controller`, `points`: `kind` is `pitch_bend`,
  `channel_pressure` or `control_change`; `controller` is the controller number, 0 unless the
  kind is `control_change`; each point is `{ time, value }` with an integer value).
- `"type": "audio"`, then `media` (ref), `region` (`{ start, end }` in samples), `gain_db`
  (decimal), `follow_tempo` (boolean), `stretch_mode` (`stretch`, `repitch`), `transpose_cents`
  (integer), `warp` (`origin`: `file`, `detected`, `guessed` or `manual`; `markers`: list of
  `{ sample, tick }`).

**Envelope**: `id`, `target`, `points` (list of `{ time, value }` with a decimal value).

**Target** (variant): `{ "type": "mixer", parameter }` (`volume`, `pan`),
`{ "type": "send", target }` (ref of the return track),
`{ "type": "device_parameter", device, parameter }` (ref, string).

**Reading is strict.** Refused, with an error that names the place (a JSON pointer such as
`/tracks/0/mixer/pan`, or line and column for malformed text):
- an unknown key, a missing key, a value of the wrong JSON type, a key that appears twice;
- an integer where the model's type cannot hold it, a decimal where an integer is expected, a name
  that is not in the lists above, an identity that is 0, not below `next_id`, or used twice;
- `-0` for any number, upper-case digits in a colour, a `hash`, `state` or `path` of the wrong
  shape: what has one meaning has one spelling;
- a byte order mark, a null byte, text that is not UTF-8, anything after the document, nesting
  deeper than 32, an integer outside 64 bits, a decimal outside what a double holds;
- a file larger than 256 MiB (the tree built while parsing is some twenty times the text);
- anything the model's own validation refuses (ranges, order, overlaps, references that lead
  nowhere, `next_id` above 2^62). The model has the last word; nothing is loaded in part.

Accepted although the writer never produces it, for the sake of editing by hand: keys in any
order, any white space, and an integer literal where a decimal is expected (`"volume_db": -6`).

## Versions and migration

1. `"format"` is a single integer at the top of the document.
2. A migration is a function from the parsed JSON of version N to the parsed JSON of version
   N+1. Migrations work on the JSON tree, not on model classes, so old shapes never need old
   classes kept alive. Each is handed a context: the version it is given, the version the file
   had, the file's `written_by`, and a list to add warnings to when a step cannot carry
   something over exactly; the warnings are returned with the loaded document.
3. Loading: check the size, parse, check the version, refuse if it is newer than this build
   knows or older than any version LINA wrote, apply migrations in order, then build the model
   strictly from the current shape.
4. **Every change of shape is a new version.** Because an unknown key is an error, an older
   build cannot read a file that a newer build wrote with so much as one added key; it must be
   told so by the version number, which is what makes "refused with a clear message" (doc 10)
   true. There are no optional or ignored keys.
5. When a document was of an older version, the first save keeps the original in `backup/` with
   its version number in the name (`project.v1.lina`), where later saves do not replace it.

## Identities

1. Every media reference, scene, track, slot, device, automation envelope, clip and note has an
   integer `id`. Sends, parameter values, controller data and points have none.
2. `next_id` in the document is the counter. An identity is assigned when the object is created
   by a command, not when it is saved, and is never reused within a project. The counter stops at
   2^62: the model refuses a command that would need more, and a document with a larger counter
   is refused on load, so the counter can never wrap round to identities already handed out.

## Media

1. A file copied into the project is stored in `media/` under its original name plus the first
   eight hexadecimal digits of its content hash. Importing content that is already in `media/`,
   under whatever name, finds the existing file. A file of the same name with other content is
   never overwritten: the import fails.
2. A media reference in the document holds the path, the file size, and the full content hash.
3. A path has one of two shapes, told apart by its first character:
   - **inside the project folder**: relative to it and normalised, that is segments separated by
     single slashes, none of them empty, `.` or `..` (`media/kick-3f9a1c2e.wav`);
   - **a file referenced where it is**: absolute, starting with `/`.
   Anything else is refused on load and on save, so no path in a document leads out of the
   project folder through relative segments (doc 10, untrusted projects).
4. Hashing is SHA-256. The implementation lives in `lina_format`, because the state store names
   files by content and has to compute the name itself; the import service above the format
   library (doc 13) calls it from its worker. Files are copied and hashed in pieces, never read
   whole into memory.

## Plugin state

1. A plugin's opaque state is stored as a binary file in `state/`, named by the SHA-256 of its
   content with `.bin` appended. The device in the document refers to it by that name.
2. Reasons: megabytes of encoded data inside the text document would defeat reading and
   comparing it; and files named by content never need rewriting, which makes saving safe.
3. Storing content that is already there writes nothing, after checking that the file really
   holds it; a file that does not hash to its name is replaced on store and refused on load.
4. **State files are not deleted by saving.** They are collected by an explicit operation that
   the owner of the session calls with the complete list of names it can still reach: the
   document in memory, its undo and redo history, a save that is queued. Kept in addition are the
   names that `project.lina`, every document in `backup/` and the recovery copy refer to. If one
   of those files is there but cannot be read, nothing is deleted. (Collecting inside the save,
   as first proposed, deleted state that only the undo history still needed.)

## Saving

1. Order of operations:
   1. before the save, new files are stored in `media/` and `state/` (names are content-derived,
      so existing files are never modified);
   2. every state file the document refers to, and every media file it says is inside the
      folder, is looked for. The ones that are not there are reported to the caller, sorted, so
      that the application can warn the user; the save goes on, because a project with missing
      files stays usable (doc 10). Nothing about it is silent;
   3. the current `project.lina` is copied to a temporary file in `backup/` and flushed;
   4. the new document is written to `project.lina.tmp` and flushed, with the permissions of the
      file it replaces (a new project gets the process's defaults);
   5. `project.lina.tmp` is renamed over `project.lina` and the folder is flushed: once both
      have succeeded the save stands;
   6. the backup's temporary file is renamed to its final name, replacing the previous backup
      only now;
   7. `recovery/` is removed.
2. The current document is copied, not moved, to `backup/`: a move would leave a moment in which
   there is no `project.lina` at all. With the copy, `project.lina` is a complete document at
   every instant, and a crash at any step leaves either the old document or the new one.
3. A failure up to the rename in step 5 leaves `project.lina` and the previous backup exactly as
   they were and removes both temporary files. Failures in steps 6 and 7 are reported to the
   caller but do not fail the save.
4. The name of the backup: `project.lina` normally; `project.v<N>.lina` when the document being
   replaced is of the older format version N; `project.unreadable.lina` when its version cannot
   be made out (damaged, or not a project). Only the version is looked at, not the whole
   document, and a current file that cannot be read does not stop the user from saving. It is
   kept, under a name of its own, so that it does not take the place of an older backup that is
   good.
5. Serialising happens on the main thread into memory and is checked there: the document must
   pass the model's validation and the checks of the reader, so what is written is always
   something that loads. Writing and flushing happen on a worker.

## Recovery copy

1. Same encoding, written to `recovery/project.lina` through a temporary file, a flush and a
   rename, at most once a minute while there are unsaved changes (doc 10; interval is open
   there). The library reads no clock; the interval is the caller's.
2. On opening a project, the recovery copy is offered whenever it exists and its bytes differ
   from `project.lina` (or there is no `project.lina`). Since a save removes the copy, one that
   is there was written after the last save. File times are not used to decide, only shown:
   they are coarse on some file systems and change with clock steps and sync tools.
3. As built on 2026-11-23, the application offers an existing recovery copy when opening a
   project but does not yet write one; writing it waits for file work to move off the main
   thread (doc 13, open question 5).

## Lock

1. While a project is open, the instance holds a kernel lock on `.lock`: an open-file-description
   lock (`fcntl` with `F_OFD_SETLK`) on the open file. The kernel releases it when the instance
   closes the file or dies, however it dies, so there is no stale lock to detect and no race
   between two instances taking one over. The file is removed when the lock is let go.
2. The file holds three lines for people to read: process identifier, host name, and the
   application's name and version. They decide nothing: process numbers and host names repeat
   across containers and machines. This is the one place a host name is written; the file is
   not part of the saved project.
3. Outcomes of trying to take the lock:
   - *acquired*;
   - *held elsewhere*: another running instance has the project open; the user is warned and
     shown who, if the file says so legibly;
   - *unsupported*: the file system has no locks. Nothing is held; the application says so and
     lets the user decide whether to go on without the protection;
   - *read-only*: the folder cannot be written to (reported as an error of its own kind), so the
     application can offer to open the project without saving.
4. As built in the application: a folder without locks or one that cannot be written to is
   opened only after the user agrees, and a project opened read-only is saved with Save As.
   Save As needs an empty folder. It refuses a project that keeps media or plugin state inside
   its folder until copying them into the new folder is built. Opening the project that is
   already open says so instead of reporting a lock held elsewhere.

## View state

`view.lina` uses the same layout and strict reading, with a version number of its own:

| Key | Type | Meaning |
|---|---|---|
| `format` | integer | `1`. |
| `view` | name | `session` or `arrangement`: which view has the keys (both are shown, stacked; doc 12). |
| `arrangement_zoom` | decimal | Horizontal zoom of the arrangement, greater than 0. The unit is a placeholder (a factor, 1 for the default width), to be fixed when the arrangement view exists. |
| `arrangement_scroll` | ticks | The arrangement position at the left edge. |

It is written through a temporary file and a rename. There is no migration chain for it yet; a
view state that cannot be read is dropped in favour of the defaults.

## Testing

1. Golden files: `tests/golden/v<N>/` holds at least one project per format version (doc 14).
   Expected lines and values are also written out by hand in the tests, so that a member swapped
   the same way in writer and reader is caught.
2. Round trip: for generated documents, save, load, save gives identical bytes.
3. Damaged input: files cut short at every byte, with changed bytes, with deep nesting and huge
   numbers, give an error or a valid document, never a crash.
4. Faults: every file operation of a save is made to fail, and to be the last before a crash, in
   turn; the project and its backup must survive each.
5. A fuzz target feeds arbitrary bytes to the loader; any crash is a defect (doc 10). Its seed
   corpus is `tests/format/fuzz/corpus/`.

## Open questions

Proposed earlier, still open:

1. File extension and names: `project.lina` and `view.lina`?
2. JSON was chosen over XML and TOML for being simple to write canonically and widely readable.
   Any objection?

Confirmed by the owner 2026-11-22: the format as built, including plugin state in separate
files named by content, the required `written_by` string, the record shapes (named members,
media referred to by identity, variants as objects with `"type"`), no optional or ignored keys,
and the lock held through the kernel.

As built 2026-11, still to confirm:

7. `backup/project.unreadable.lina` stays until someone removes it, and while it is there no
   state file is collected. Should a later save or the user remove it?
8. A file system without locks: the project opens only if the user agrees to go on unprotected.
   Or refuse?
9. The limits: 2^62 for the identity counter, 256 MiB for a document, 1 GiB for one plugin
    state read in one piece, nesting depth 32.
10. The zoom unit in `view.lina`.
11. SHA-256 in `lina_format` rather than in the import service above it.
