# Songs and setlists

Status: the first simple-song implementation is integrated into main, with inline
selections and references to other setup documents. Included in the combined
firmware deployment; host tests and the combined firmware build pass. Physical
qualification is pending. See the [controls, file format and limits](../cntrl/maker/morningstar/mc/songs-and-setlists.md).
This develops the performance organization ideas in [features.md](features.md).

## Design priority

Design the common path for the 90% of users whose songs are simple. A song should
be as easy to create as choosing a sound and giving it a name. A setlist should
be as easy to create as picking songs in order. Neither task should require a
computer, a section editor, MIDI programming or a text-entry session.

More elaborate songs can have sections, but the ordinary song and setlist screens
must remain simple. Start with complete support for simple songs and live setlist
editing before expanding section behavior.

## Basic concepts

- A song is a reusable named performance item that recalls an existing patch
  selection. A single selection is a complete song.
- The song library contains the songs available on the pedal, independently of
  whether they appear in a setlist.
- A setlist is an ordered list of references to library songs. The same song can
  appear in several setlists or more than once in one setlist.
- Removing a setlist entry removes that occurrence only. It does not delete the
  library song or affect its occurrences in other setlists.

A song may contain its selection inline or reference a selection in another
setup document. Inline selections remain independent of later source edits.
References resolve when recalled; missing or ambiguous sources report an error
without sending MIDI or advancing the setlist.

Keep library editing and setlist editing visibly distinct. An action called
“Remove from setlist” must never delete a song from the library.

## Creating a simple song

From the existing patch browser or current selection, provide “Save as song.”
Use the selected patch as the song's sound and its name as an editable default
song name. One confirmation should create a usable song without typing.

The minimum workflow is:

1. Choose the sound.
2. Choose “Save as song.”
3. Accept the suggested name or edit it.

Saving or naming a song must not itself transmit MIDI. Auditioning a sound uses
the ordinary explicit recall action. A song with no sections recalls its selection
once when the performer activates it; no artificial “verse” or “main” section is
required in the UI.

Allow renaming and changing the associated selection later. Distinguish songs
with the same displayed name using their associated sound or other short context
in the picker; names alone must not determine which song a setlist references.

## Building a setlist entirely on the pedal

Provide “New setlist” from the setlist browser. Suggest a unique usable name,
such as “Setlist 3,” and make renaming optional.

Open a song picker immediately. Browsing and adding should be the primary
interaction, with search available as an aid rather than a required keyboard
step. Provide alphabetical navigation and quick movement through a long library.

Selecting “Add” appends the highlighted song. Keep the picker open after each
addition, preserving its position, so a performer can add several songs without
reopening menus. Show the number of songs added and the last addition. Repeated
songs are allowed, and each addition receives clear feedback.

“Done” returns to the ordered setlist. The performer can then move or remove an
entry and start the set. An empty setlist displays “Add songs” and sends nothing.

For example, creating a five-song setlist should require one entry into the song
picker, five additions and one exit. It must not require naming five entries or
confirming a separate save dialog for each one.

## Editing during a performance

Expose “Edit setlist” from the performance screen without navigating back through
setup management. The main actions are:

| Action | Behavior |
| --- | --- |
| Add next | Open the song picker and insert after the current song. |
| Add to end | Open the same picker and append songs. |
| Remove from setlist | Remove the highlighted occurrence and offer Undo. |
| Move | Select an occurrence, choose its new position, and confirm. |
| Done | Return directly to the performance screen. |

For “Add next,” multiple additions retain the order in which the performer picked
them: adding A and then B produces current song, A, B, previous next song.
Maintain the insertion position while the picker remains open.

Show the current song and the next song while editing. Browsing, adding, removing
and reordering must not recall patches or transmit musical messages. The sound
already in use continues until the performer explicitly activates another song.

Make removal quick and reversible: use a clearly labelled removal action and an
immediately available Undo instead of a confirmation dialog for every entry.
Provide undo of the most recent setlist edit, including an addition or move.
Keep this separate from performance undo, which concerns recalled sounds.

## What happens to the current and next songs

Track the current occurrence, not merely its numeric position. Inserting or
removing earlier entries must not silently change which song is considered active.

- Removing the next song makes the following remaining song the new next song.
- Moving the current occurrence changes its position, but does not recall it.
- Removing the current occurrence leaves its sound active and labels it as removed
  from the setlist. Preserve its former place as the continuation point: Next
  chooses the first remaining song after that place.
- A second occurrence of the same song remains a separate occurrence. Removing
  one must not select, remove or rename the other.
- At the end of the setlist, show “End of set.” Next does not wrap or send anything.
  The performer can append songs and continue, or explicitly restart the setlist.
- If all entries are removed, retain the active sound and offer “Add songs.”

The displayed next song must always match what the next explicit activation will
recall, including after an edit or its undo.

## Saving and returning to the performance

Persist confirmed setlist edits automatically on the pedal. Do not make performers
remember a separate save step. Give brief save feedback; if saving fails, report
that the edit was not saved and preserve the last successfully saved setlist.

Store songs and setlists as user data on SD, independently of firmware updates.
Load records and entries on demand, with bounded working memory and no fixed
song, setlist or entry-count ceilings. Reuse
the existing setup storage and transfer approach where appropriate. A completed
edit must survive a restart, and an interrupted write must not destroy the last
saved setlist. Persisting edits does not imply automatically recalling a sound
on boot.

Computer download and upload should preserve pedal edits. A downloaded setlist
must include or accompany the songs it needs, and an upload must explicitly
handle replacement of an existing edited setlist. Ordinary operation after the
songs are on the pedal must not depend on a computer connection.

## Optional song sections

Expose sections only when someone chooses to add them. A more complex song can
then have named sections such as intro, verse, chorus and solo, each recalling
its associated selection. Reuse the same song picker and setlist editing flows
for both simple and sectioned songs.

The feature ideas also mention different section orders for separate occurrences
of a song. Define that advanced behavior separately before implementation; it
must not introduce required fields or extra steps for a simple song. The first
implementation does not need that capability to make setlists useful.

## Interaction details to settle before implementation

Choose exact switch assignments through a small on-pedal prototype. Keep the
same browse, select, back and performance-recall meanings across song and setlist
screens. Editing may use hands, but should work through the pedal's own controls.
Do not overload the normal recall action with a destructive edit.

Check picker legibility, duplicate-name disambiguation and movement through a
large library on the actual displays. Establish whether alphabetical jumping is
sufficient before adding more navigation mechanisms.

## Acceptance scenarios

1. A user saves a simple song from an existing patch without entering text or
   creating sections.
2. With the computer disconnected, a user creates a five-song setlist from the
   library in one visit to the picker, then starts performing.
3. During a song, the user adds two requests immediately after it. The active
   sound is unchanged, and the requests become next in the chosen order.
4. The user removes the next song and immediately sees the correct replacement;
   Undo restores the original order without transmitting MIDI.
5. The user removes or moves the current occurrence without changing its sound
   or losing a predictable continuation point.
6. Two occurrences of the same song can be edited independently. Removing either
   leaves the library song and other setlists intact.
7. The user adds a song after reaching the end and continues without restarting
   the setlist or reconnecting a computer.
8. Saved edits survive restart; an interrupted save retains the previous saved
   state. Downloading the edited setlist preserves its order and song references.

## Additional work beyond the prompt

None.
