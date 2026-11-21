# 10 Project file format: requirements

Status: **draft**. This doc states what the saved form of a project must guarantee. How it is
encoded on disk is doc 16, which waits for the stack decision. **(R1)** marks what the first
release needs. Items marked *proposed* are suggestions waiting for a yes or no. All statements
are `[own design]`.

## What a saved project consists of

1. **(R1)** A project is a folder. It holds the project document, the audio recorded in that
   project, and cached analysis data (waveform overviews, detected tempos).
2. **(R1)** Cached data can be deleted at any time without losing anything; LINA recomputes it.
3. **(R1)** Audio files the user imports are copied into the project folder by default, so a
   project folder is complete without the user doing anything. A setting turns copying off, and
   files are then referenced where they already are. Decided 2026-11-13.
4. **(R1)** Importing the same file twice into one project stores it once.
5. **(R1)** For projects that do contain outside references, a "collect files into project"
   command copies every referenced file into the project folder and updates the references.
6. **(R1)** Nothing outside the project folder and its referenced files is needed to reopen a
   project, apart from the plugins it uses (doc 03, rule 6).

## Media references

1. **(R1)** A reference to a file inside the project folder is stored relative to the folder.
   Moving or renaming the folder breaks nothing.
2. **(R1)** A reference to a file outside the folder stores its location together with enough
   about the file (size and a fingerprint of its content) to notice that it has been replaced
   and to recognise it if it turns up somewhere else.
3. **(R1)** Opening a project with missing files lists them all at once and lets the user point
   to a folder to search. The project is fully usable in the meantime (doc 07).

## Versions and migration

1. **(R1)** Every project document records the format version that wrote it.
2. **(R1)** Every release of LINA opens every project written by every earlier release. Old
   projects are upgraded in memory by a chain of single-step migrations, one per format version.
3. **(R1)** For every format version there is a checked-in sample project that the test suite
   loads forever (golden files).
4. **(R1)** Opening a project written by a newer LINA is refused with a clear message. LINA never
   opens a project it does not fully understand and then saves over it.
5. **(R1)** Saving a project that was opened from an older format does not overwrite the old
   file silently: the old document is kept beside the new one.

## Fidelity

1. **(R1)** Loading a project and saving it again without changes, with the same version of
   LINA, produces a byte-identical document. The saved form therefore contains no timestamps, no
   random identifiers generated at save time, and no ordering that depends on memory layout.
   (The document names the application version that wrote it, doc 16; saving with another
   version changes that one line.)
2. **(R1)** Save, load, save round-trips the complete document model: every property in docs 03
   to 09 that the user can set is either saved or explicitly listed as session-only.
3. **(R1)** Session-only state, not saved: undo history, selection, which clips are playing,
   transport position. Saved: zoom and scroll positions, which view is showing, the start marker
   and loop range.
4. **(R1)** The stable identities of tracks, scenes, clips, notes and devices (doc 03, rule 5)
   are stored, not regenerated on load.
5. **(R1)** Numbers are written and read the same way regardless of the system's language and
   regional settings.

## Readability

1. **(R1)** The project document is text that a person can read and that a version control
   system can compare meaningfully: one property per line where practical, stable ordering.
   Decided 2026-11-13.
2. **(R1)** A plugin's saved state is opaque data from the plugin and is the one exception. It is
   kept out of the text document, in a file of its own inside the project folder that the
   document refers to (doc 16). The document itself holds the plugin's format, identifier, name,
   vendor and version, so that a missing plugin can be named (doc 09).

## Safety of saving

1. **(R1)** Saving never leaves a half-written project. The new document is written completely
   beside the old one and then swapped in. A crash or power loss during a save leaves either the
   old project or the new one intact.
2. **(R1)** The previous saved document is kept as a backup until the next successful save. A
   save that fails leaves the backup that was there untouched.
3. **(R1)** If two running copies of LINA open the same project, the second is warned.
4. **(R1)** What is saved can be loaded: a document is checked before it is written, and a save
   that would produce a document LINA could not reopen is refused instead.
5. **(R1)** Files that an open project may still need are not deleted behind its back: plugin
   state that only the undo history refers to stays until the session says it is done with it
   (doc 16, "Plugin state").
6. **(R1)** A project with missing files can be saved ("Media references" 3). When a save finds
   that the document refers to a plugin state file, or to a media file inside the project
   folder, that is not there, it saves all the same and the user is told which files are
   missing; it is never silent.

## Crash recovery

1. **(R1)** While a project has unsaved changes, LINA periodically writes a recovery copy beside
   it, without interrupting audio and without the user noticing. *Proposed* interval: at most
   one minute of work can be lost.
2. **(R1)** After a crash, opening the project offers the recovery copy and shows when it was
   written. It is offered whenever it differs from the saved document; the times of the files
   are shown, not relied on. Declining keeps the last saved version; the recovery copy is not
   deleted until the user saves.
3. **(R1)** Audio being recorded is written to disk continuously and is recoverable up to the
   last few seconds, even if the project itself was never saved (doc 05).
4. Later: recovery that replays every edit since the last save, so that nothing is lost.

## Opening untrusted projects

1. **(R1)** Opening a project never runs code contained in it. A project from a stranger can do
   no more than load the plugins it names, which the user already has installed.
2. **(R1)** A malformed or hostile project document produces an error message, never a crash,
   and cannot make LINA use memory without limit: documents beyond a fixed size are refused
   (doc 16). The loader is tested with deliberately damaged input.
3. **(R1)** File references in a project cannot make LINA write outside the project folder, and
   LINA does not read or replace its own files through symbolic links placed in the folder.

## Privacy

1. **(R1)** A project document contains no information about the user or the machine other than
   the locations of files referenced outside the project folder. With the default of copying on
   import, or after "collect files into project", it contains none. (It does name the version
   of LINA that wrote it. The lock file that exists while a project is open names the machine;
   it is not part of the saved project, doc 16.)

## Later

- Project templates and a user-chosen default project.
- Saving a single track or clip as a reusable file.
- Exchange formats shared with other DAWs.

## Open questions

1. Is losing up to one minute of work after a crash acceptable for the first release?
2. With copying on import as the default, large sample libraries get duplicated into every
   project that uses them. Should LINA share one copy between projects where the file system
   allows it, or is plain copying fine?
