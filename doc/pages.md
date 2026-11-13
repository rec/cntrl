# Pages

A setup has a catalogue of named pages, arranged in independently cycling
regions. Page types include
`patch-switcher`, `status`, `tap-tempo`, `firmware`, `cable-test`, `panic`,
`trs-ports`, `expression`, `clock-transport`, `clock-tempo`, `keyboard`,
`midi-monitor`, `midi-looper`, `arpeggiator`, and `None`.
Select a region or a page in the Pages and tools menu. F and the existing
reverse-page control cycle only the focused region. Page and region names
contain 1 to 31 printable ASCII characters.

Each patch-switcher page keeps its own navigation, sent-state and undo history.
All configured patch-switchers continue executing queued patches and delays while
hidden. They share the setup's patch library, cycles and display templates.
Shared hardware services, including the MIDI clock, remain shared across pages.

## Regions and focus

Each region contains at least one page, and every page belongs to exactly one
region. Its first page is initially selected. `layout.order` lists every region
once, in physical display order. When omitted or empty, it follows region declaration
order. Any page type can occupy any region.

`layout.focus` names the region receiving page-specific controls. It defaults
to the first physical region. A thin border marks its visible windows, and the
menu marks its region with `*`. Changing focus preserves all page selections.
Selecting a page also focuses its region. Held gestures are cancelled when
focus changes. Management and emergency controls remain global.

When regions are omitted or empty, multi-display targets put Patch Switchers in a
`performance` region and other pages in a `tools` region, omitting empty groups.
A single-display target uses one `main` region containing the entire catalogue.
Omitting `pages` uses the built-in catalogue. These defaults focus the first
physical region, usually the Patch Switcher on a multi-display target.

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
min_windows = 1

[pages.monitor]
type = 'midi-monitor'
min_windows = 1

[pages.live]
type = 'patch-switcher'
min_windows = 1
give_up_windows = ['right', 'center', 'left']

[pages.alternate]
type = 'patch-switcher'
min_windows = 1
give_up_windows = ['left', 'right', 'center']
```

## Window allocation

Every visible page type except Patch Switcher uses exactly one window. This
includes Status, Tap Tempo, MIDI Monitor, MIDI Looper and Arpeggiator. Their
`min_windows` values are forced to one when compiling, exporting or allocating
an existing saved setup. They do not acquire spare windows. The blank `None`
placeholder keeps minimum and maximum zero.

For Patch Switcher, `min_windows` replaces the former page setting `windows`
and defaults to one. An explicit zero lets it receive no windows while its
controls and background work remain available. It can acquire spare windows up
to the target's display count.

Every selected page receives its minimum first. `layout.left_wins = false`,
the default, awards remaining windows from the rightmost region toward the
left, up to each page type's maximum. Set it to `true` to award them from the
left instead. Each region occupies a contiguous span. Any surplus beyond all
maxima stays black. Compilation rejects a setup if any selectable combination
cannot meet its minimums, or a minimum exceeds its type's maximum.

On a three-display pedal, the example gives Live two windows and Status one.
Selecting Monitor also gives Live two windows and Monitor one. Reverse
`layout.order` to move tools to the left. Allocation priority affects spare
windows only where more than one selected Patch Switcher can use them.

Patch Switcher's `give_up_windows` lists logical content slots to surrender
first, defaulting to the reverse target display order. Surviving slots keep
their logical order and map into the region's physical span. These names do
not anchor content to a physical side.

The encoded layout supports up to 255 named pages, subject to available memory.
Display IDs and window counts follow the target's declared displays.

Page preferences, expression assignments and keyboard shortcuts remain under
`[page_settings]`. Patch display templates remain `[patch_views.NAME]`, referenced
by `[windows.NAME]` through `view = 'NAME'`.

## Empty setups

`pages = {}` configures no pages. Animations may still run when configured with
`splash_movie`, `idle_movie` and a positive `screensaver_timeout`. Management tools
remain available for selecting another setup.

## Compiled format

Setup format 39 uses `CPAG` version 2, containing target display IDs, named
page records with minimums, ordered region cycles, allocation direction and
initial focus. Recompile authored setups after changing page `windows` to
`min_windows`. Editable export preserves the compiled region layout.

Upgrading an older layout with one Patch Switcher and multiple displays keeps
Patch Switcher on the left and its original tool cycle on the right. The tools
region starts focused, so F cycles tools while unused tool buttons continue to
control the visible Patch Switcher. The old Patch Switcher position in that
cycle becomes a blank `full-performance` page, giving Patch Switcher every
display without replacing its navigation or undo state. Existing page names
and order are retained. Layouts already using regions retain their saved layout.
