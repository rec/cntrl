# Apps and pages

A setup contains named app instances and an ordered cycle of pages. All apps
continue running, including queued MIDI and delays. One app is visible at a time.
Select an app by its name in the Apps and tools menu.

The current app type is `patch-switcher`. Instances share the setup's patch
library, cycles and display templates, and keep independent navigation and send
history. The firmware supports up to four instances and 32 page entries. App
names contain one to 31 printable ASCII characters.

Omitting `apps` and `pages` preserves the default setup. It has one patch switcher
and starts on Status in the rightmost window. The page cycle is Status, Firmware,
MIDI Cable Test, Panic, TRS Ports, Expression, MIDI Clock, Clock Tempo, Keyboard,
Tap Tempo, then Status again. F advances through that cycle. The existing reverse
browsing chord and page controls continue to work.

## Window allocation

Each page entry has a `type` and a `windows` count from zero to three. Pages take
the rightmost physical windows. The visible app uses the remaining windows.
An app's `give_up_windows` lists its logical windows in the order to surrender
them. The default is right, center, left, preserving the current patch-switcher
layout. Surviving app windows retain their order and pack into the available
physical windows from the left.

For example, a two-window page leaves one app window visible. With the default
order, that is the app's left window. With `['left', 'right', 'center']`, it is
the app's center window.

Pages spanning several windows repeat their heading and distribute their body
rows across those windows in reading order. The tempo flasher appears in the
first page window. A zero-window page still has its normal controls and behavior.
The special `None` page always has zero windows and no behavior, giving the
visible app all three windows.

```toml
pages = [
    {type = 'status', windows = 2},
    {type = 'tap-tempo', windows = 1},
    {type = 'None'},
]

[apps.live]
type = 'patch-switcher'
give_up_windows = ['right', 'center', 'left']

[apps.alternate]
type = 'patch-switcher'
give_up_windows = ['left', 'right', 'center']
```

Available page types are `status`, `tap-tempo`, `firmware`, `cable-test`, `panic`,
`trs-ports`, `expression`, `clock-transport`, `clock-tempo`, `keyboard`, and `None`.
Repeated entries may use different window counts. Shared page preferences,
expression assignments and keyboard shortcuts live in `[page_settings]`.

## Empty setups

Both collections may be explicitly empty. To run only animations, configure
`splash_movie` and/or `idle_movie` and a positive `screensaver_timeout`. Empty
movie categories remain disabled even when there are no apps or pages.

```toml
apps = {}
pages = []
```

With only apps, the visible app has all three windows. With only pages, the page
cycle works normally and unused windows remain black. The management tools remain
available for selecting another setup.

## Patch display templates

The old field-list tables are now `[patch_views.NAME]` and window references use
`view = 'NAME'`. These templates describe patch text and styles, independently of
the setup's page cycle. For example:

```toml
[patch_views.patch]
fields = ['patch_cycle.name', 'display_name']

[windows.next]
view = 'patch'
```

Setup format 31 stores app names, surrender order, page types and window counts
in a checked `CLAY` section before the patch-name metadata. Recompile earlier
setups with the current compiler before installing them on format-31 firmware.
