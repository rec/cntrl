# Pages

A setup has a catalogue of named pages, arranged in independently cycling
regions. Page types include
`patch-chooser`, `status`, `time`, `cable-test`, `panic`,
`midi-monitor`, `midi-looper`, `arpeggiator`, `banked`, `language`,
`setups`, `songs-and-setlists`, `switches`, `settings`, `pong`, `doom`,
`memon`, and `None`.
In Patch mode, press F
to cycle pages in the focused region. In Song and Setlist modes, the default
controls are hold F to browse utility pages forward, or click F and hold the
second press to browse backward.
Page and region names contain 1 to 31 printable ASCII characters.

The default utility pages for testing are Status, MIDI Cable Test, Panic,
MIDI Monitor, MIDI Looper, Arpeggiator, Time, Pong, Language, Setups,
Songs and setlists, Switches, Settings, and Calibration, in that order.
Press C on Setups, Songs and setlists, Switches, or Settings to open its editor.
Pong requires three displays. Time and Language require two.
MIDI Looper and Arpeggiator need an enabled MIDI input and output.
Banked views are configured with the Banked patches they display.
When the setup inherits the whole default cycle, Patch Chooser mode puts one
Empty stop at the end and Setlist mode puts it at the start. Empty gives the
performance view the full display. Authored page and region lists use only the
Empty entries explicitly listed, in their authored positions. A utility cycle
with no Empty moves directly between its configured pages when it wraps.
Setups using the default pages and automatically generated regions start with
the tools region focused, so F browses this utility cycle. Explicit layout focus
settings take precedence.

Each patch-chooser page keeps its own navigation, sent-state and undo history.
All configured patch-choosers continue executing queued patches and delays while
hidden. They share the setup's patch library, cycles and display templates.
Shared hardware services, including the MIDI clock, remain shared across pages.

## Settings

On the MC6 Pro, press C and F together to open Settings directly from the
browser or setlist. Press them together again to return to the previous
view. Pending edits bring up **Save**, **Discard**, and **Keep editing**.
An authored C+F navigation chord takes priority. Include Settings in an
explicit page list if you use C+F for navigation. Timing-sensitive utility
pages such as Time and Panic keep their own switch timing while focused.

Open Settings to edit the loaded setup's brightness, screensaver timeout,
or USB MIDI setting. In browser mode, the current patch is also available
when it selects a program for an instrument. Its MIDI channel edit applies
to every patch that uses that instrument in the setup.
Under **Pedal**, choose the setlist to open on the next boot. Choose **None**
to start in the Patch Chooser.
In setlist mode, **Use at startup** opens the same editor with the current
setlist selected.

Open **Preferences** under **Pedal** for device-wide defaults. A/B browse
fields and D/E move between groups. In a value editor D clears the device
value so the setup supplies it, and E changes the step size for larger
numbers. In MIDI clock outputs, A/B choose an output and C toggles it.
Press F to review before saving. An explicit setup value appears beside
the device value when the setup takes precedence.

Open **Setlists** from **Settings** to reorder a saved setlist. Browse to
the setlist, press C to open it, then use A/B to choose an occurrence by
its numbered position. Press C to start moving it and A/B to choose the
destination. The review shows both positions before you save. C+F also
opens the review if you try to return to playing with a move pending.
Press D at an entry to add a song after it, add one at the end, or remove
that occurrence. Each change has a review and save prompt.

Open **Songs** from **Settings** to rename a song or reorder or remove its
patches. The song picker starts on the playing song when one is active.
Saving a rename updates each setlist occurrence and the live song name.
The review shows the new name or the patch and position before saving.

Under **Pedal → Contact**, use A/B to change the character in brackets and
D/E to move the cursor. C opens the actions for inserting a space, inserting
a line break, or deleting a character. F opens the save prompt when the text
has changed. Contact saves to the pedal's preferences and appears on the
Contact screen in Status.

Adjustments are drafts. Back from a value returns to its field. Back from
the field list offers to save and reload the setup, discard the draft, or
keep editing. Saving the loaded setup stops current playback during reload.

Open **Setups** from **Settings** to browse the saved setups. Choose one to edit
its patches or setup settings. Saving edits to an inactive setup stores a
revision and keeps the loaded setup playing. Saving the loaded setup uses
**Save and reload setup**.

## Changing setups

Selecting another setup stops the current setup's activities instead of refusing
the selection because they are running. The destination is validated first.
The Setups menu shows `Changing setup` while the handoff completes; USB selection
acknowledges completion after the destination is active.

The handoff cancels pending patch sequences, delays, repeated switch gestures,
song execution and banked-page actions. It stops file players, loopers, LFOs,
clock generators, arpeggiators, forwarding, processing queues and MIDI monitoring.
It releases notes, pedal holds and keyboard keys, restores relay resting states,
clears expression assignments and stops old animations, pulsing and games.
An active MIDI-file recording is finalized and retained as the latest recording.
An in-progress save finishes before its buffers are released. The patch editor's
existing Save/Discard decision still protects unsaved setup edits.

MIDI cleanup has a one-second budget, so an unplugged or blocked output cannot
prevent changing setups indefinitely. Incomplete delivery is reported and old
pending traffic is discarded. Bytes already accepted by physical transmitters
can finish transmitting. Storage errors and an invalid or unaffordable destination
remain errors; they are not reported as a successful switch.
If activation fails after cleanup, the old setup stays stopped and another
selection can be attempted.

The new setup starts with fresh runtime state and its own configured defaults.
Its system generators may start automatically. Old switch holds and delayed
actions do not carry into it. Browsing between pages in the same setup continues
to preserve background performance.

## Regions and focus

Each region contains at least one page, and every page belongs to exactly one
region. Its first page is initially selected. `layout.order` lists every region
once, in physical display order. When omitted or empty, it follows region declaration
order. Any page type can occupy any region.

`layout.focus` names the region receiving page-specific controls. It defaults
to the first physical region for authored layouts. A thin border marks its
visible displays. Changing focus preserves all page selections.
Selecting a page also focuses its region. Held gestures are cancelled when
focus changes. Management and emergency controls remain global.

When regions are omitted or empty, multi-display targets put Patch Choosers and Banked pages in a
`performance` region and other pages in a `tools` region, omitting empty groups.
A single-display target uses one `main` region containing the entire catalogue.
Omitting `pages` uses the built-in catalogue. An authored region list can
pick from that catalogue by page name.

Pong requires three displays. The default Patch Chooser can yield all its
displays while Pong is selected, preserving its state and background execution.
Controllers with fewer than three displays omit Pong from the implicit
catalogue. Selecting None returns all displays to the Patch Chooser.

The default tools rotation ends with Pong, Doom, and Memon, followed by the
None stop. It includes each game when the target supports it. Doom uses one
display. Press C on that page to start the standalone game. An explicit page
catalogue can include `type = 'doom'`
in its chosen rotation. [Doom build and launch](../cntrl/maker/morningstar/mc/doom/README.md)

Setups, Songs and setlists, and Switches each use one utility display. Back from
an editor returns to its page. To switch from a set to the Patch Chooser, open
Songs and setlists and choose **Patch Chooser**. Replace references to the retired
`menu` page with the individual pages you want, then recompile the setup.
TRS roles and expression assignments remain setup configuration.

## Memon

Memon appears in the default tools rotation on compatible three-display
targets. To include it in a custom page catalogue, add a page with
`type = 'memon'`. The [MC6 Pro example](../cntrl/maker/morningstar/mc/emulator/memon.toml)
opens directly to the game.

Press C to start and F to leave. D, E, A and B play tiles 1, 2, 3 and 4.
Each correct press while repeating the sequence adds one point. The left
display shows the score, saved high score, round, position and elapsed time
for the current run. A new high score is saved on the pedal's SD card.

```toml
[page_settings.memon]
device = 'din'
channel = 1
notes = [0, 4, 7, 10]
note_offset = 60
velocity = 100
note_duration = 0.250
gap_duration = 0.250
error_duration = 2.0
timeout = 5.0
show_remaining_time = true
repeat_requests = false
timed = true
cooperative = false
mono = true

[page_settings.memon.style]
font = 'AtkinsonNextSemiBold'
foreground_color = 'white'
background_color = 'black'
tile_colors = ['0x884DFF', '0xFF7A1A', '0x00B8A0', '0xFFF1D6']
color_offset = 64
```

Each Note On is followed by breath control (CC 2) at 100, and each Note Off
by breath control at 0. With `mono = true`, the error notes play legato and
divide `error_duration` evenly. Breath stays on from the first Note On until
all notes are released together at the end. Set `mono = false` for a simultaneous
error chord. Set `error_chord` to a list of up to four intervals from
`note_offset` to override the default four game notes.
The response timer restarts after each correct press. Set `timeout = 0` or
`timed = false` for an untimed game. With `repeat_requests = true`, double-tap
any tile switch during your turn to replay the sequence after a one-second
pause, then start again from the first tile with a fresh timer. With
`cooperative = true`, choose the opening note, then add a new note after
each correct replay. The computer also adds one note before the next replay.

## Pong

`type = 'pong'` reserves three displays for one continuous court. Both paddles
are controlled by players; matches end at 11 points. The serve control also
pauses and resumes play, and starts a new match after a win. Leaving the page
pauses the game and releases held paddle controls. Each named Pong page retains
its own scores and positions until another setup is loaded.

The previous/previous-section controls move the left paddle down/up;
confirm/back move the right paddle down/up. Next serves or pauses, and
next-section leaves for the next page in the region. Include another page in
that region as an exit destination. Pong uses the existing page renderer and
does not send MIDI.

```toml
[pages.Game]
type = 'pong'

[pages.Status]
type = 'status'

[regions.main]
pages = ['Game', 'Status']
```

```toml
[layout]
order = ['performance', 'tools']
left_wins = false
focus = 'performance'

[regions.performance]
pages = ['live', 'alternate']

[regions.tools]
pages = ['status', 'monitor']

[pages.status]
type = 'status'
min_displays = 1

[pages.monitor]
type = 'midi-monitor'
min_displays = 1

[pages.live]
type = 'patch-chooser'
min_displays = 1
give_up_displays = ['right', 'center', 'left']

[pages.alternate]
type = 'patch-chooser'
min_displays = 1
give_up_displays = ['left', 'right', 'center']
```

## Display allocation

Time and Language each use exactly two displays. Time's left display selects the
clock source and toggles Send; its right display shows Tap Tempo. See
[Shared MIDI clock](midi-clock.md) for controls and clock settings. The former
standalone `tap-tempo` page is retired; recompile setups that contain it.

Status, MIDI Monitor, MIDI Looper, Arpeggiator and the other utility
pages use exactly one display. Their
`min_displays` values are forced to one when compiling, exporting or allocating
an existing saved setup. They do not acquire spare displays. The blank `None`
placeholder keeps minimum and maximum zero. Banked and Pong require three
displays.

For Patch Chooser, `min_displays` replaces the former page setting `windows`
and defaults to one for explicitly configured pages. The default catalogue
sets it to zero so full-width tools can occupy all displays. Zero lets it receive no displays while its
controls and background work remain available. It can acquire spare displays up
to the target's display count.

Every selected page receives its minimum first. `layout.left_wins = false`,
the default, awards remaining displays from the rightmost region toward the
left, up to each page type's maximum. Set it to `true` to award them from the
left instead. Each region occupies a contiguous span. Any surplus beyond all
maxima stays black. Compilation rejects a setup if any selectable combination
cannot meet its minimums, or a minimum exceeds its type's maximum.

On a three-display pedal, the example gives Live two displays and Status one.
Selecting Monitor also gives Live two displays and Monitor one. Reverse
`layout.order` to move tools to the left. Allocation priority affects spare
displays only where more than one selected Patch Chooser can use them.

Patch Chooser's `give_up_displays` lists logical content slots to surrender
first, defaulting to the reverse target display order. Surviving slots keep
their logical order and map into the region's physical span. These names do
not anchor content to a physical side.

When Patch Chooser has one display, it shows the last successfully sent patch
using `displays.sent`, regardless of `give_up_displays`. The display is blank
until a patch is sent.

The encoded layout supports up to 255 named pages, subject to available memory.
Display IDs and display counts follow the target's declared displays.

Page preferences and expression assignments remain under `[page_settings]`.
Keyboard output uses patch actions. Patch display templates remain `[patch_views.NAME]`, referenced
by `[displays.NAME]` through `view = 'NAME'`.

## Empty setups

`pages = {}` configures no pages. Animations may still run when configured with
`splash_movie`, `idle_movie` and a positive `screensaver_timeout`.

## Compiled format

Setup format 39 uses `CPAG` version 2, containing target display IDs, named
page records with minimums, ordered region cycles, allocation direction and
initial focus. In existing authored setups, rename `[windows.NAME]` to
`[displays.NAME]`, `give_up_windows` to `give_up_displays`, and `min_windows` to
`min_displays`. Editable export preserves the compiled region layout.

Upgrading an older layout with one Patch Chooser and multiple displays keeps
Patch Chooser on the left and its original tool cycle on the right. The tools
region starts focused, so F cycles tools while unused tool buttons continue to
control the visible Patch Chooser. The old Patch Chooser position in that
cycle becomes a blank `full-performance` page, giving Patch Chooser every
display without replacing its navigation or undo state. Existing page names
and order are retained. Layouts already using regions retain their saved layout.
