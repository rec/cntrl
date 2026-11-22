# Scenes

A performer should be able to write a song as an ordered series of scenes.
Each scene gives them the sounds, controls, and information they need for one
part of the song. For a verse, chorus, and repeated verse, they should only
have to define two scenes.

## Authoring a song

Build on named patches, bindings, and the song's existing ordered `patches`
list. A scene is a patch with a binding map that occupies all the performance
windows. Keep a song step as one occurrence in the list, so the same scene
can appear at several steps. Actions in a patch sequence still run together at
one step. [Patch bindings](../doc/control-bindings.md)

Make scenes the starting point in the song creation workflow and examples.
Start with a song name and a first scene. Let the author name the scene, pick
the patch to run on entry, and assign actions to buttons using their physical
labels. Derive the button captions from those assignments. Supply the standard
display automatically, including when some buttons have no assignment.
Use the existing step editor to insert, reorder, or reuse scenes.

Use a small worked example as the authoring check. Define **Verse** and
**Chorus**, give Chorus a different button action, and put
`patches = ['verse', 'chorus', 'verse']` in the song. The third step should
recall Verse with its original controls and captions.

## Scene display

Use three horizontal segments per window. The top and bottom captions sit
beside their corresponding physical buttons and say what a click will do.
Resolve the active bindings, including overrides, before choosing the captions.
Keep the positions fixed when the scene changes. Foreground, background, and
font may vary using the existing song, patch, and binding styles.

| Segment | Left window | Center window | Right window |
| --- | --- | --- | --- |
| Top | Upper button action | Upper button action | Upper button action |
| Center | Current scene | Current song | Setlist and general information |
| Bottom | Lower button action | Lower button action | Lower button action |

Use the scene name on the left. Use the song presentation rules below in the
center. Use the setlist name on the right when performing a set. A position
such as “2 of 8” is a candidate for general information. During standalone
song performance, omit the setlist name.

With two windows, use the left middle band for the scene and the right middle
band for the song. With one window, divide its middle band into two rows of
equal height, with the scene above the song. On both smaller layouts, omit
the setlist and general information from the scene view. Keep the upper and
lower button captions in their usual positions.

## Navigation

Define four button combinations in the setup using the controller's existing
control labels. Resolve them in the shared input handler so the pedal and
emulator agree.

| Action | Where it applies | What it does |
| --- | --- | --- |
| Song forward | Set performance and standalone song performance | Advance one position along the existing song-step path. |
| Song backward | Set performance and standalone song performance | Go back one position on the same path. |
| Page forward | Everywhere | Scroll forward through the utility-page list. |
| Page back | Everywhere | Scroll backward through the utility-page list. |

The setup designates the forward and back controls. A performer presses them
directly on any scene in song or set mode. Songs and scenes cannot replace
either action. Preserve the empty
positions before, between, and after songs. Selecting a page must preserve
the current song and scene.

For page navigation, use the setup assignment by default. A song can override
it and an individual scene can override the song. On leaving a scene or song,
restore the applicable enclosing assignment. Put the empty utility page
(`None`) first in song mode, so the performer can see the complete scene.
The empty utility page is a display choice. An empty song position is a step
in performance navigation.

## Song image and name

Add optional `image` and `display_name` to the song. Resolve the image path
relative to its source file. During `cntrl` compilation on the computer,
prepare a bitmap at the exact pixel dimensions allocated to the song on the
selected target, including the shorter song row on a one-window controller.
Include the prepared asset in song transfer.
The pedal draws the bitmap directly. Size calculation, cropping or fitting,
and pixel conversion all happen on the computer.

| Song supplies | Center segment shows |
| --- | --- |
| Image and display name | Bitmap and display-name text together |
| Image only | Bitmap alone |
| Display name only | Display-name text |
| Neither, with a full song name | Full song name |
| Neither, without a full song name | Filename stem for a single-song source, e.g. `Blue Train` from `Blue Train.toml` |

Keep presentation separate from the song name used by setlist entries.
If a source file contains exactly one song and its name is omitted, use the
filename stem as its song name at compilation. Apply the usual song-name
validation and uniqueness rules. In a file containing several songs, require
a name on each song. Songs created on the pedal keep the name assigned at
creation. Changing a display name affects only the caption.

## Verification

- Create and perform the Verse/Chorus example both standalone and in a set.
  Check the top and bottom captions against the physical buttons and check
  the center segments after advancing and going back.
- Configure dedicated forward and back buttons. Advance and go back directly
  on each scene, including scenes with competing bindings. Check that pressing
  a combination triggers its action once without firing a member button's
  click action.
- Override page navigation first in a song, then in one scene. Leave each
  scope and verify its predecessor takes effect. Return to the empty utility
  page and confirm the same scene and song step are still active.
- Exercise every row of the song presentation table. Verify the compiled
  bitmap dimensions against the target segment, and check that upload,
  download, and emulator playback retain the artwork and display name.
- Check the scene and song placement on one-, two-, and three-window targets.
  Test an unnamed song in its own file, a duplicate filename-derived name,
  and an unnamed entry in a multi-song library.

## Additional work beyond the prompt

None.
