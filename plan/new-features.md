# New features

## Data-driven page buttons and emulator tooltips

Deferred. Keep the current fixed button and display tooltip slots for now.

Give each built-in page one definition of its button bindings. Each binding
would name a controller role, an event such as press or hold, and a stable
action ID. The firmware would use the definition to dispatch events to its
existing action handlers. The native emulator would report the resolved action
IDs for each physical button, and the desktop would look up the corresponding
localized tooltip text. Authors could provide one translation per action
regardless of the number of buttons or the page's screen position.

For example, the Time page would bind `confirm` on press to **Tap Tempo** and
`confirm` on hold to **Clear Tap Tempo**. On the MC6 Pro, the target's role map
would resolve both to C. Another controller could assign the same role to a
different switch. Moving the focused Time page from one region to another
would preserve the binding.

Preserve the current [routing order](../firmware/include/cntrl/ui/controls.h)
and eligibility rules. Open menus and editors take precedence, some actions
depend on page state, and an unclaimed event can reach a setup binding.
Resolve tooltip actions from the same bindings and active routing rules the
firmware uses. A separate list of button descriptions would drift from actual
behavior.

Start with [Time](../firmware/include/cntrl/ui/page_host.h) and verify that
press, hold, and navigation behave as before on six- and eight-button layouts.
Then try a stateful page such as Language or Looper before extending the
approach to other built-in pages. Help for setup-defined bindings remains an
open design question.

Display tooltips need their own mapping to named content. A tip should move
with its content when a page moves between regions. If the layout hides that
content, its tip should disappear without shifting to a different display.

### Additional work beyond the prompt

None.
