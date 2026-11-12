# Page layout independent of left and right

Implemented in setup format 39 with CPAG version 2. Automated validation covers
authoring, firmware allocation and UI navigation. Physical pedal verification
remains to be performed.

Allow any page type to occupy either side of the pedal. A per-setup allocation
strategy determines which side receives spare windows first. Status can occupy
a single window beside a Patch Switcher that expands into the remaining space.

## Agreed allocation rules

- Rename the authored page setting `windows` to `min_windows`. It is the
  minimum number of windows the selected page must receive.
- Ordinary pages default to `min_windows = 1`. An explicit zero permits a
  selected page to receive no windows while its background work continues.
  The `None` page has a minimum and maximum of zero. A zero minimum still
  allows a page to receive spare windows up to its type's maximum.
- Page-type code exposes the maximum useful window count. This capability
  describes the renderer and is independent of authored setup preferences.
  Status has a maximum of one. Patch Switcher can use every available target
  window. MIDI Monitor can use up to three.
- The other utility pages remain expandable up to the target window count.
- Use per-setup `layout.left_wins: bool = False` to distribute spare windows.
  False gives the rightmost region first claim. True gives the leftmost region
  first claim. Priority follows region placement in the selected direction.
- Reject a layout at compilation when its simultaneous minimum requests
  exceed the target's display count. Also reject a minimum above its page
  type's maximum or the target's available count.

## Regions, placement and page cycles

A region contains an ordered cycle of named pages and has one selected page
at a time. Either region can contain any supported page type. Keep the
existing named page catalogue and reference those pages from region lists.
Every page belongs to exactly one region, so its instance and navigation
state have an unambiguous owner. Regions must contain at least one page.

The proposed configuration keeps `[pages.NAME]` definitions and assigns them
to regions.

```toml
[layout]
order = ['performance', 'tools']
left_wins = false
focus = 'performance'

[regions.performance]
pages = ['live', 'alternate']

[regions.tools]
pages = ['status', 'monitor']

[pages.live]
type = 'patch-switcher'
min_windows = 1
give_up_windows = ['right', 'center', 'left']

[pages.alternate]
type = 'patch-switcher'
min_windows = 1

[pages.status]
type = 'status'
min_windows = 1

[pages.monitor]
type = 'midi-monitor'
min_windows = 1
```

`layout.order` places region spans in the target's physical display order.
Require this list to contain every region exactly once. `layout.left_wins`
chooses the direction of priority across that order and defaults to `false`.
`layout.focus` names the initially focused region. The first page in each region is selected
initially. Region and page names use the existing printable-name conventions.
When focus is omitted, use the first physical region. When order is omitted or
empty, use region declaration order. Omitted or empty regions default to performance and tools
on multi-display targets, omitting empty groups, or one main region on a
single-display target. Performance contains Patch Switchers; tools contains
the remaining catalogue.

With three displays, the example starts with Live across the left two displays
and Status on the right. Selecting Monitor gives it two displays and leaves
Live with one. Changing only `layout.order` to `['tools', 'performance']`
places Status or Monitor on the left. Status still receives one window because
of its type's maximum. With Monitor on the left, `left_wins = false` gives Live
two windows and Monitor one. Setting it to `true` gives Monitor two and Live one.

## Focus and controls

One region has control focus. The Pages and tools menu shows regions and their
selected pages and lets the user choose focus or select a page in a region.
Selecting a page through that menu also focuses its region.
The existing page-cycle and reverse-page controls cycle only the focused
region. Cycling one region preserves all other regions' current selections.
Selecting a Patch Switcher page affects only its containing region.

Page-specific switch handling goes to the focused region's selected page.
Selecting focus updates its visible indication without reallocating windows.
Preserve global management and emergency controls. A focused page can have a
zero-window allocation, so the menu must still expose its name and focus.
Complete or cancel held input gestures in the old view when changing focus,
so their release cannot trigger an action in the newly focused page.

Each Patch Switcher instance retains its own navigation, sent state and Undo
history. Background patch execution and shared MIDI services continue across
focus and page changes. Multiple views of a shared service, such as the looper,
continue to observe that service's existing shared session.

## Allocation procedure

For each legal combination of selected pages, allocate their minimums first.
With `left_wins = false`, visit regions from the end of `layout.order` toward
its beginning. With `left_wins = true`, visit them from the beginning toward
the end.
Give each selected page as many remaining windows as it can use, bounded by
its page-type maximum. Both strategies satisfy every minimum before awarding
spare windows. Once those counts are settled, place their contiguous spans
in the authored physical order.
Windows within a span follow the target's declared display order.

For a three-display target with Status and Patch Switcher each requesting
one window, the resulting counts are one and two. Status cannot absorb a
spare window even when it has first priority. With two flexible pages, the
higher-priority page receives the spare window. Any windows beyond all page
types' maxima remain blank.

Validate every selectable combination during compilation. Because each region
selects independently, summing the largest `min_windows` value in each region
is sufficient to check that every combination fits. Report the region and page
names contributing to an impossible allocation. Firmware loading must check
the same constraints before admitting a compiled layout.

## Renderer contract

Pass each visible page its first physical window and allocated window count.
The renderer uses local window indices within that span. Remove assumptions
that utility pages start at the right edge or that Patch Switcher occupies
the leftmost available displays.

Keep logical Patch Switcher content selection separate from physical placement.
Its existing `give_up_windows` ordering determines which logical content to
drop as its allocation shrinks. The surviving content keeps its logical order
and maps into the allocated physical span. Maintain selected patches, sent
state and Undo history independently of that span's physical position.
Names in `give_up_windows` identify logical Patch Switcher content slots.
They do not anchor those slots to physical left or right displays.

A page type exposes its maximum through the shared page-type implementation
contract. An expanding type can report the target's available window count.
Use the same capability values in authoring validation and firmware admission,
with an automated agreement check if the two implementations need separate
representations. Reject unsupported allocations when loading a compiled setup.
Set the maximum of each remaining page type after reviewing its renderer and
supported layouts. Preserve MIDI Monitor's dedicated one-, two- and three-window
views. Keep these capability decisions beside the page-type implementation.

## Implementation and verification

1. Extend the authoring models and layout compiler with regions, independent
   placement order, the per-setup `left_wins` boolean, initial focus, and
   `min_windows`. Apply the existing page-count, controller-count and target
   display limits. Update
   tracked example setups and the generated sample configuration to the new
   schema. Preserve unrelated page preferences and patch display templates.
2. Revise the compiled page-layout records and format version through the
   existing format workflow. Carry region membership, cycle order, minimums,
   physical order, `left_wins` and initial focus through compilation, validation,
   editable export and setup bundles. Recompile the tracked fixtures.
3. Replace the current utility-first, right-edge allocation in
   `firmware/include/cntrl/page_layout.h` and `ui/page_host.h` with one shared
   allocation result for all regions. Store each region's selected page and
   allocate view state using the current configured-count allocation approach.
4. Render all selected region views into their allocated spans and route
   page-specific controls through focus. Update the Pages and tools menu and
   the page configuration guide to describe the same model.
5. Test omitted `left_wins` defaulting to false, both explicit boolean values
   and mirrored region orders. With two flexible pages, verify that changing
   `left_wins` changes which receives spare windows, and that reversing both
   placement and `left_wins` preserves counts per region. Test Status capped
   at one, Monitor capped at three, zero minimums, blank surplus space and single-display
   targets. Reject non-boolean `left_wins` values. Check invalid placement orders,
   unknown or repeated page references and
   a layout where an initially valid selection has a later impossible pairing.
6. Exercise independent page cycles and focus changes through the normal UI
   path. Check Patch Switcher logical content selection, preserved history,
   background execution, held-control release and shared-service views. Verify
   compile/export round-trips and firmware rejection of malformed records.
7. On hardware, select a tool beside a Patch Switcher in both physical orders.
   Confirm display placement, focus indication and switch actions against the
   authored configuration.

## Additional work beyond the prompt

None.
