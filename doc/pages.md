# Pages

A setup has one ordered collection of named pages. Page types include
`patch-switcher`, `status`, `tap-tempo`, `firmware`, `cable-test`, `panic`,
`trs-ports`, `expression`, `clock-transport`, `clock-tempo`, `keyboard`,
`midi-monitor`, and `None`.
Select a page in the Pages and tools menu or cycle through them with F and the
existing reverse-page control. Page names contain 1 to 31 printable ASCII characters.

Each patch-switcher page keeps its own navigation, sent-state and undo history.
All configured patch-switchers continue executing queued patches and delays while
hidden. They share the setup's patch library, cycles and display templates.
Shared hardware services, including the MIDI clock, remain shared across pages.

## Split-screen allocation

Each page has a `windows` count. A patch-switcher defaults to all target displays;
a utility page defaults to one, except `midi-monitor`, which defaults to two.
`None` defaults to zero. Explicit zero-window
pages retain their controls and background behavior. Unused displays are black.
The monitor requires one, two or three windows; see [MIDI monitor](midi-monitor.md).

A utility page takes its requested windows from the right. The last selected
patch-switcher remains visible in the windows to its left, limited by that
patch-switcher's own window count. Its `give_up_windows` names the logical windows
to surrender first. By default this is the reverse of the target display order.
Surviving logical windows retain their order and pack from the left.

Selecting another patch-switcher restores that instance's state and gives it its
requested windows. Selecting a utility page keeps that patch-switcher alongside
it. Utility pages spanning multiple windows repeat their heading and distribute
body rows in reading order, except the monitor, which has separate Incoming,
Combined and Outgoing views according to its window count. The tempo flasher
occupies the first utility window.

```toml
[pages.status]
type = 'status'
windows = 1

[pages.live]
type = 'patch-switcher'
windows = 3
give_up_windows = ['right', 'center', 'left']

[pages.alternate]
type = 'patch-switcher'
windows = 2
give_up_windows = ['left', 'right', 'center']
```

The first configured page is initially selected. The first patch-switcher is
available alongside it. Omitting `pages` starts with Status beside a default
patch-switcher and includes the other built-in pages in the normal cycle.
The firmware supports up to four patch-switchers and 36 named pages in total.
Display IDs and window counts follow the target's declared displays.

Page preferences, expression assignments and keyboard shortcuts remain under
`[page_settings]`. Patch display templates remain `[patch_views.NAME]`, referenced
by `[windows.NAME]` through `view = 'NAME'`.

## Empty setups

`pages = {}` configures no pages. Animations may still run when configured with
`splash_movie`, `idle_movie` and a positive `screensaver_timeout`. Management tools
remain available for selecting another setup.

## Compiled format

Setup format 36 uses a `CPAG` section containing target display IDs and one
ordered sequence of named page records. The old `apps` collection is removed;
recompile authored setups with named `[pages.NAME]` tables. The existing offline
historical setup converter now emits the same unified page records.
