# Songs and setlists

The first implementation supports simple songs, an on-pedal song library and
editable setlists. It lives on branch `codex/songs-and-setlists`. It has not been
flashed or physically qualified. Song sections remain deferred as described in
[the plan](../../../../plan/songs-and-setlists.md).

## On the pedal

Open the module menu with A+D and choose **Songs and setlists**. A/B browse,
C confirms and F returns. The module offers Perform setlist, Setlists, Songs,
Save as song and Save reference.

In the ordinary patch browser, highlight a patch before opening the module.
**Save as song** creates an inline song using that selection and its displayed
name. No naming step is required. **Save reference** instead remembers the
selected setup document, instrument alias, program and logical bank. Neither
operation sends MIDI.

Under Songs, C opens Rename song or Use selected patch. The latter replaces the
song's sound with an inline copy of the patch browser's highlighted selection.
The song retains its ID and its setlist occurrences. Rename uses A/B to change a
character, D/E to move the cursor, C to save and F to cancel. The same editor is
available for setlist names.

Under Setlists, choose **New setlist**. It receives a unique default name and
opens the alphabetically sorted song picker. C adds the highlighted song and
keeps the picker open; A/B browse and D/E jump between initial letters. F returns
to the ordered performance view. Song IDs and selection context distinguish
otherwise identical displayed names.

In the performance view, C recalls the next song. A/B highlight an occurrence
for editing; they do not recall it. D opens the edit menu:

- Add next inserts after the active occurrence. Successive additions retain
  their chosen order.
- Add to end appends without restarting an exhausted setlist.
- Remove from setlist removes the highlighted occurrence only.
- Move lets A/B choose its destination and C commit it.
- Rename setlist changes the title.
- Undo edit restores the most recent saved edit. It sends no MIDI.
- Restart setlist makes the first entry next, without recalling it.
- Done returns to the performance view.

Current and next songs remain visible while editing. Removing the current
occurrence leaves its sound active, marks it removed and retains the continuation
point. Undo can restore that occurrence. At the end, C sends nothing and reports
End of set. Choosing another setlist or restarting also sends nothing.

Successful song recalls clear the ordinary patch browser's previous recall
history and sent-state markers, since those no longer describe the active sound.
Setlist edit undo is independent of sound recall. The song module does not offer
a separate sound-undo command.

## Inline and referenced selections

Inline selections store output, MIDI channel, program, optional bank-select
bytes, volume and pan. Editing the original setup does not change an inline song.

A reference resolves the current saved source setup each time the song is
recalled. Its key is the instrument alias, program and logical bank. Changes to
that selection's route, channel, volume and pan therefore take effect on the next
recall. Changing its key or deleting the document makes the reference unresolved.
If multiple matching occurrences have different selections, recall fails as
ambiguous. A missing or invalid reference never falls back to an old snapshot,
sends nothing, and does not advance the setlist.

## Computer files and transfer

A single TOML document contains the song library and its setlists. Example:

```toml
[[songs]]
id = 1
name = 'One Love'
patch = {output = 'din', channel = 1, program = 12}

[[songs]]
id = 2
name = 'Linked sound'
reference = {document = 'pman', instrument = 'pman', program = 7, bank = 0}

[[setlists]]
id = 3
name = 'Tonight'
songs = [1, 2, 1]
```

Exactly one of `patch` or `reference` is required. Channels and program numbers
are one-based. Optional inline `msb`, `lsb` and `volume` are 0..127; `pan` is
-64..63. Omitted bank/controller values send no corresponding controller change.
An omitted reference bank selects an unbanked source selection.

Song and setlist IDs must be positive and unique across the file. Occurrence IDs
are allocated automatically. Downloads include `entry_ids` and `next_id`; retain
these when editing so occurrences keep their identities and deleted IDs are not
reused. Duplicate song names and repeated occurrences are allowed.

```bash
uv run --frozen cntrl songs copy songs.toml \
  --target cntrl/maker/morningstar/mc/patch-switcher-target.json \
  --port /dev/cu.usbmodem144682801
uv run --frozen cntrl songs download ./downloaded --port /dev/cu.usbmodem144682801
```

Copy requires `--overwrite` to replace an existing library. Referenced setup
imports also require `--target`; an inline-only library does not need it. Referenced documents
must accompany the library as `DOCUMENT.toml`, for example `pman.toml` beside
`songs.toml`. They use the normal setup copy path, including font assets and its
existing overwrite checks. Downloads include those documents and their fonts.
A reference uses the setup's name on the pedal, rather than a host absolute path.

Close the song editor before computer transfers. Transfers verify the library's
CRC and perform byte-for-byte read-back. An incomplete library upload is not
committed. Source setup copies and the library replacement are separate commits;
a later failure does not roll back successfully copied source setups.

## SD storage and working memory

There are no configured ceilings on the number of songs, setlists or occurrences.
The variable-length library lives on SD. A song is read as one record; a setlist
header and individual entries are read on demand. The alphabetical picker scans
for the next matching name without allocating a sorted library in RAM.

Edits and undo stream a new generation through at most 256-byte I/O chunks.
Three SD generation files preserve the active state and the previous edit while
another is written. A fourth SD file stages incoming uploads. Upload reception
also streams chunks directly to SD and validates the completed data before
committing it. No library-sized RAM buffers or entry arrays remain.

The native song controller occupies 440 bytes and its transfer controller is
less than 128 bytes on the test host. These sizes do not grow with the library.
The SD adapter retains four file handles; transient song records and I/O buffers
also occupy bounded working memory. The desktop TOML tools may hold the library
in host RAM.

Confirmed edits save automatically under `/cntrl/songs/`, with generation
counters, CRCs, completion markers and read-back verification. A failed save
leaves the active library unchanged; boot chooses the newest valid complete
file. Invalid existing storage is reported instead of silently overwritten.
Undo preserves the ID allocation counter so removed occurrence IDs cannot be
reused accidentally. Boot does not recall a song or resume a setlist automatically.

Counts and IDs use 32-bit fields, and a generation file must fit the underlying
SD filesystem and the format's approximately 4 GiB file range. Storage must have
room for saved generations and an incoming upload. These are representation and
capacity constraints, rather than the former 64-song/16-setlist/64-entry caps.
Names and patch-selection fields retain their existing format bounds.

The payload begins with three little-endian 32-bit values: next ID, song count
and setlist count. Song records follow. Each setlist has its ID, name and a 32-bit
entry count followed by exactly that many occurrence/song ID pairs. There are no
unused padded collection slots. Committed files use the `CSG2` envelope.

Saving and boot verification stream over the file, so their work grows with the
amount of stored data. Import checks use repeated scans to validate identities
and references with bounded RAM; large imports can take longer. SD latency has
not been qualified on the pedal. Songs and setlists survive firmware updates
that preserve SD data.

No SD latency, physical power-loss behavior or display readability has yet been
measured on the pedal. Those checks are required before deployment. Alphabetical
jumping is the initial picker navigation; text search and optional sections are
not implemented in this version.

## Verification, 2026-10-27

All 345 Python tests pass, including variable-length Python-to-C++ transfers of
130 songs and 20 setlists with 130 entries each. Tests reject duplicate IDs,
missing song references, unconfirmed replacement and interrupted uploads.
Sanitizer-enabled native tests create 130 songs, 22 setlists and a 105-entry
setlist; edit and undo entries beyond the former limits; and verify recovery on
boot. The test storage rejects read/write requests larger than 256 bytes. Static
assertions keep the core controller below 1 KiB and transfer controller below
128 bytes. Existing reference recall, picker, failed-save and rename tests pass.

Both native suites, Ruff, formatting, ty and pyupgrade checks are part of the
final verification. The build has not been flashed; host checks do not establish
physical power-loss behavior, SD latency or pedal usability.

The final combined image has SHA-256
`f77bae8e563d5d692bac2a32d72a963580a331df06b3724d739b1771edd59e18`.
It leaves 55,456 bytes for stack/local variables and 308,736 bytes for heap
allocation before loading custom fonts. Permanent RAM2 use falls by 89,696 bytes
compared with the fixed-buffer implementation. Both native suites and the final
firmware build passed without compiler warnings. No pedal upload was performed.

## Rebase verification, 2026-10-28

Rebased onto main commit `36c830b`, preserving setup revision history, patch-mode
startup, brightness feedback and the Original firmware module. Song test peers
implement the updated brightness interface. Returning to stock waits for an
in-progress song upload as well as other storage/MIDI activity.

All 349 Python tests, both native firmware suites, Ruff, formatting, ty and
pyupgrade checks passed. CLI startup was checked for the main command, songs
commands and setup history. The combined image built without compiler warnings;
SHA-256 is `f32567c52fd627889ccd8a97a13c45d3b2849d2cb0888af0804c1a91fc8bfeeb`.
The combined feature set leaves 55,232 bytes for stack/local variables and
242,656 bytes for heap allocation before custom fonts. This build was not flashed.
