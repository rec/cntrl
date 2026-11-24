# On-pedal Settings

Give the performer one Settings page for every parameter that can be edited
and saved on the pedal. Make it available in browser and setlist modes.
Opening Settings, inspecting an object, and leaving without saving must
preserve the current performance and send no MIDI.

## What the performer sees

1. **Enter and return.** Include Settings in the default page cycle. Provide
   a direct Settings shortcut in browser and setlist modes, including on a
   full-pedal patch and in authored cycles that omit Settings or Empty.
   Open the editor over the performance and restore the previous page, focus,
   and song position on return. Opening or closing it at a song boundary must
   leave the next song waiting for the performer's first step.
   Prototype C+F on the MC6 Pro, with a visible **C+F Settings** hint on entry
   to performance mode and **C+F Return to playing** in the editor. Check
   conflicts with authored bindings, navigation and system gestures before
   adopting that mapping. Consuming the shortcut must also consume its releases.
2. **Start from something familiar.** On **Current**, label each shortcut with
   its role and actual name. Between songs show **Between songs**, **Next song ·
   Mercy** when there is a next song, and **Setlist · Friday show**. Offer
   **Pedal**, **Setups**, **Songs**, and **Setlists** for browsing elsewhere.
   On Friday show's setlist offer **Use Friday show at startup**. Open the
   device-wide startup setting with that choice pending and the explanation
   **Applies whenever this pedal boots**.
3. **Show the scope before adjustment.** From a patch using Keys, open
   **MIDI channel**. Display **Keys · MIDI channel**, the current value, and
   **All patches using Keys in this setup** together. For the Verse step of
   Mercy referencing Clean, offer **Only Verse in Mercy** and **Every use of
   Clean**. Explain at the choice that a local copy stops following changes to
   Clean. Identify the occurrence by position when names repeat. Group longer
   field lists by task, and show the source of an inherited value.
4. **Edit one named object at a time.** Label the active switches for each
   screen. On the MC6 Pro, A/B browse or adjust, C opens a field or accepts
   its value into the draft, D/E jump groups or move the text cursor, and F
   goes back one level. Back from a field keeps its draft value and highlights
   that field in the list, marked **Changed**. Keep the saved value active
   until Save, and label any immediate preview before adjustment. Revert a
   preview on discard.
5. **Finish the edit.** Leaving an object with pending changes, including
   through **Return to playing**, offers **Save**, **Discard**, and **Keep
   editing**, with the object's name visible. Save and Discard continue to the
   requested destination. Keep editing returns to the draft. If saving
   reloads the active setup, say what playback or position will change and
   label the committing control **Save and reload setup**. Explain a blocked
   or failed save and keep the draft available for correction or discard.

## Ownership and persistence

- Device-wide preferences use the existing device settings validation and
  journal. Preserve the checks for active MIDI, reload-sensitive changes, and
  compatibility with stored setups. Expose live runtime controls separately
  from persistent settings when the same value has both meanings.
- Setup, instrument, patch cycle, patch, scene, and nested patch edits use the
  owning setup or song library. Resolve a reference to its owner before
  offering a shared edit. Reuse the existing local-copy behavior when the
  user edits just an entry. Use the existing setup revision and source-sync
  paths for changes saved to a setup.
- Song edits use the shared song library. Setlist editing covers its name and
  ordered song entries, plus any metadata the setlist already owns. Preserve
  repeated occurrences as distinct positions. Display the startup set setting
  under Pedal because it is a device-wide choice.
- Show inherited values and their origin where that origin is known. Offer
  **Use inherited value** only when the owning storage can actually clear the
  override. Keep host authoring defaults distinct from live device defaults.
- Validate the changes for an owner together before saving. Keep the saved
  state and existing references intact if validation or persistence fails.
  When an edit requires applying new settings to the active runtime, reuse the
  owning service's existing safety checks and activation procedure.

## Implementation order

1. **Prove the trip from playing to editing and back.** Prototype the first
   rehearsal task below before expanding the field list. Verify that performers
   can discover both shortcuts and that existing switch bindings still work.
2. **Inventory the fields.** Review the device registry, setup editor, song
   and setlist editors, and authoring schemas. Record each field's owner,
   type, limits, inheritance rules, save path, and effect on playback.
   Classify each field as writable on the pedal today, requiring new on-pedal
   save support, or describing host or build inputs. Keep genuine read-only
   values and transient performance state distinct from editable parameters.
3. **Unify existing edits.** Introduce Settings and the Current shortcuts.
   Surface the device registry and the existing setup, song, and setlist
   edits through their present storage services. Verify parity before
   removing any duplicate entry points.
4. **Fill the remaining gaps by owner.** Add each missing field only with a
   complete validation and save path. Reuse numeric, choice, text, list, and
   binding editors across fields. For values whose compiled representation
   needs to change, establish how the pedal can validate and rebuild the
   affected data before offering an edit. Track those gaps explicitly so a
   computer-only authoring option is distinguishable from unfinished pedal
   support.
5. **Review coverage.** Check the field inventory in both modes and verify
   that opening Settings directly preserves explicit page lists exactly.

## Verification

### Unaided rehearsal tasks

Give performers the tasks without switch instructions. Before they save, ask
what will change, who else will be affected, and what they expect to hear.

1. From the current patch, change Keys to channel 7 and get back to playing.
   Check that the performer finds Keys, correctly predicts the scope, and
   recognizes any reload consequence before committing.
2. From the empty position before the first song, open Settings and return.
   Repeat between songs and with an explicit cycle omitting Settings and Empty.
   Verify that the next forward step still triggers the intended song entry
   and first patch exactly once.
3. Move the second occurrence of Mercy earlier in Friday show, then select
   Friday show for startup. Verify the intended occurrence moved and that
   selecting the startup set did not start it during rehearsal.
4. Change Verse in Mercy only. Check that the performer can explain what
   happens to Verse if someone edits Clean tomorrow. Repeat with a shared
   edit, then discard it from the return-to-playing prompt.
5. Correct a typo in the middle of the second line of contact text. Back out
   to inspect the draft, keep editing, then save and reopen it.

On the physical pedal, observe switch presses, wrong turns, unintended saves,
requests for help, and captions that require bending down. Record where they
occur and repeat those tasks after changing the labels or controls.

### Behavior checks

- Open Settings from both modes. Edit the active object and browse a different
  saved object while a scene is running. Check that browsing and discarding
  leave playback, the current step, and MIDI output unchanged.
- Verify that an instrument channel edit affects its users, that editing one
  entry preserves other references, and that editing the original changes all
  of its users. Check both loading and clearing an override.
- Exercise numeric, choice, text, and ordered-list edits. Save, reboot, and
  confirm the effective values. Check editable export and source sync for
  setup edits. Verify that setlist edits preserve repeated song entries.
- Exercise rejected reloads, invalid values, and a failed save. Check the
  feedback and confirm that the previous saved state remains active.
- Verify behavior in the shared emulator. Qualify navigation, legibility, and
  save/reboot behavior on the physical pedal before release.

## Implemented coverage and remaining save paths

| Owner | Settings coverage | Save path |
| --- | --- | --- |
| Pedal | Startup set, including the current setlist shortcut, and multiline Contact text | Device preference journal |
| Pedal boot firmware | CNTRL or stock on controllers with a combined firmware image | Boot selection flash journal |
| Pedal language | Installed languages | Device preference journal and deferred language loader |
| Expression calibration | Heel and toe capture for enabled expression jacks, jack names, and settling time, with review before Save | Device calibration file |
| Device preferences | 33 scalar registry fields, including choices and a named clock-output picker. Port fields depend on hardware support. | Device preference journal with inherited values, setup overrides, and reload validation |
| Loaded setup | Grouped Display, MIDI, Navigation, Ports, and Time settings with value origin and a way to inherit an existing pedal preference | Setup draft, revision, and reload |
| Saved setups | The same grouped settings and the existing patch-cycle editor | Named setup draft and revision, with reload for the loaded setup |
| Setlists | Rename and browse, move, add, and remove numbered occurrences. Edit display name, text color, and background color with review. | Song library journal, with playback position tracking, startup-name recovery, and editable appearance export |
| Songs | Browse, rename, move or remove patches, and edit display name, text color, background color, and font with review | Song library journal, including setlist references and playback position, plus editable appearance export |
| Embedded song patch | Pick a supported MIDI action by its switch and trigger or activation order. Edit program, bank, volume, and pan for instrument programs, channel and program for direct Program Change, or channel, controller, and value for single Control Change. | Song library journal with a private source copy or an in-place replacement in the new revision |
| Song patch from a named setup | The same MIDI actions and fields as embedded song patches, with the choice to edit one occurrence or every use of the action in the owning setup | Private copy in the song library or a setup revision, with review before reloading the active setup |
| Setup instruments | Browse all instruments in a setup, or start with the current browser patch's instrument. Edit its MIDI channel and choose an enabled output. | Shared instrument record in the setup draft |

- Device-wide controls outside the listed coverage still need a consolidated
  Settings entry point.
- Song-step editing still needs support for the other patch fields. Fields
  derived from authoring inputs need a rebuild path for any compiled data
  they affect. Nested references into another setup still need their own
  owner resolution.
- The MC6 Pro prototype opens Settings and returns with C+F. It consumes both
  switches through release and preserves authored C+F navigation. The editor
  shows the Back chord. A launch hint appears at setlist boundaries.
  Physical rehearsal checks remain open,
  including the 45 ms window for the second switch.

### Field inventory by owner

| Owner | Fields with an on-pedal save path | Fields still requiring an editor or save path |
| --- | --- | --- |
| Pedal | Boot firmware, startup set, language, contact text, calibration endpoints, jack names, settling time, and the 33 scalar preferences | |
| Setup | Brightness, screensaver timeout, animation color cycling, target pulse, MIDI interface and USB modes, USB forwarding, pickup and pairing time, reset controls on loop stop, scrolling, fast debounce, navigation timing, TRS modes, auxiliary switch mode, Time page settings including clock outputs and presets, and patch cycles | Frame buffers, animation resources, loop-stop neutral values, other page-specific scalar settings, routing, controls, counter definitions, startup and idle movie selections, and page layout and appearance |
| Setup instrument | MIDI channel 1–16 and enabled output, shared by the instrument programs using that instrument in the setup | Profile and instrument identity. Profile choices can affect bank mappings and SysEx. |
| Setup patch | Instrument program 1–128, banks available in the assigned profile, optional volume 0–127, and optional pan −64–63 on supported program patches | Other MIDI action types and their parameters, names, style, nested actions, and binding definitions |
| Song | Name, ordered patch occurrences, display name, foreground, background, and embedded UI fonts | Installed artwork, song enter and exit actions, MIDI and bindings, and the remaining patch fields |
| Setlist | Name, ordered song occurrences, display name, foreground, and background | Installed artwork and remaining authored style metadata |

The [device preference registry](../firmware/include/cntrl/ui/device_preferences.h)
defines scalar types, bounds, and choices. Explicit setup values take priority
over pedal preferences. The pedal preference editor shows the loaded setup
value and offers to clear the device preference. The setup editor can clear a
setup override when the corresponding pedal value is available.

The [setup schema](../cntrl/patch_chooser/models.py) and
[song schema](../cntrl/patch_chooser/songs.py) define authoring values. Fields
that select installed artwork, fonts, or movies can offer those resident
assets once the owning file can be updated. Asset paths, source definitions,
style generation seeds, and target hardware declarations are compilation
inputs. Current tempo, clock send, and other performance state use their
runtime controls. Status diagnostics are read-only.

## Related work

- [Device and setup settings](../doc/global-config.md)
- [Editing patch references](../doc/patches.md#editing-patch-references-on-the-pedal)
- [Song and setlist editing](../cntrl/maker/morningstar/mc/songs-and-setlists.md#on-the-pedal)
- [Setup backup and source synchronization](setup-sync.md)

## Additional work beyond the prompt

None.
