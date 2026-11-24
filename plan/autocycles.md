# Automatic patch cycles

Let a user turn an instrument already configured in a setup into a patch
cycle by choosing it and confirming. Treat the instrument alias as a filter
in the cycle picker, like a tag. Selecting just the instrument includes its
whole named catalogue. Selecting a program tag as well narrows the cycle to
that instrument's programs with the tag. The instrument membership comes from
the catalogue and MIDI route, so the user enters it once in the picker.

## What goes in the cycle

Use the selected alias and its configured profile and MIDI route. Include
named programs in ascending bank and program-number order. Skip unnamed slots
and preserve the program numbers and bank mappings. A flat catalogue keeps its
existing Program Change routing. Match tags using the current catalogue and
setup tag rules. Programs with the same display name can appear separately
when their bank or program numbers differ.

Represent the selection once in setup source. Both creation paths produce
this form:

```toml
[patch_cycles]
Synth = { instrument = 'synth' }
'Synth Brass' = { instrument = 'synth', tag = 'brass' }
```

Expand a selector into ordinary browseable patch entries. Recompiling the
setup uses the current catalogue and tags. Existing authored lists remain
available for cycles with a hand-picked order or mixed actions.

## Create with cntrl

Add a `cntrl setup add-cycle SETUP.toml` wizard to the existing setup
commands. Load instrument definitions, profiles, and tags through the normal
setup source loader. Show a numbered list of instrument aliases, with the
entire catalogue as the default after selecting one. Offer its existing tags
as optional filters.

Show the proposed cycle name and program count before saving. Default the
name to the instrument alias, with the tag appended for a filtered cycle.
Suggest an available name if the default is taken. Enter accepts the
suggestion, and a user can type a name when they want one. Save the selector
into the setup's local source file. Use the existing compile and copy workflow
to send that setup to the pedal.

## Create on the pedal

Add **Add patch cycle** to the setup editor. Browse instrument aliases,
select **All programs** or a tag, then review the proposed name and program
count. Confirm and use the existing save flow. The resulting cycle appears
in browser mode. The same selections can be made with the pedal switches,
with typing needed only for a custom name.

Carry the configured catalogue on the pedal as a compact index of selectable
programs, including programs outside the setup's existing cycles. Prepare
their display data and resolve bank mappings and routes during compilation.
The pedal filters those existing records and saves the ordered references
for the new cycle. Share catalogue records across cycles. Include the
catalogue and the new cycle in the normal target storage, patch count, and
memory checks. If the selection is empty or the cycle will not fit, report
the reason before saving and keep the current setup usable.

Store enough information about the selector to export or sync a pedal-created
cycle back to the setup source. The exported cycle uses the same instrument
and optional tag selector as the desktop wizard.

## Verification

- Create an entire-instrument cycle and a tagged cycle through each workflow.
  Verify the same program order, display names, and MIDI routing.
- Cover sparse and banked catalogues, shared program names, and two aliases
  for the same instrument model on different outputs or channels.
- Verify that the pedal can add a cycle containing programs the current setup
  has never used. Exercise empty selections, name collisions, capacity
  limits, cancellation, and a failed save.
- Export and sync a pedal-created cycle, then recompile it after changing its
  source catalogue or tags. Verify that manually authored cycles keep their
  entries and order.

## Additional work beyond the prompt

None.
