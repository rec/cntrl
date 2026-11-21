# Emulator translation corrections

## Correction mode

Add a **Correction** toggle to the emulator. While it is on, show
**Contribute Corrections...**, **Save Corrections...**, **Discard Corrections...**,
and the read-only label **Corrections: 0**. Disable all three buttons whenever
the count is zero. Use the emulator's existing localization for this chrome.

Clicking fixed, catalogue-backed text on an emulated display opens a borderless
editor directly over that text. Give it a thin outline and match the original
position, bitmap font, size, colors, and display scaling. Place a red X and a
green square just above the editor. The red X and Escape cancel the current edit.
The green square and Return accept it. Keep keyboard focus in the editor while
it is open. Consume the click and editing keystrokes before the emulator's
switch shortcuts see them. Opening the editor over a caption must never trigger
the action it describes. Return or Escape closes the editor and returns keyboard
focus to the canvas. Finish or cancel the current edit before opening another.

Seed the edit with the current translation, including the complete string if
the display wrapped or clipped it. Accepted corrections preview immediately in
the emulated UI and follow the same message across pages. Count each changed
translation field once. Re-editing it updates the same correction, and returning
it to the original text removes it from the count. Cancelling an edit restores
the previously accepted text for that field. For example, correcting the same
caption on two pages counts once. Two messages that happen to read identically
can be corrected independently. The count is the number of accepted fields
that differ from their originals. Enable all three buttons when it is positive.

Editable text is static interface copy on the emulated displays. Authored names,
live values, formatted messages with runtime values, desktop controls, and page
tooltips stay outside the click targets. Use the language actually rendered by
the display, including English when the runtime falls back to English.

## Leaving Correction mode

If the user switches off Correction mode or quits with pending edits, show
**Unsaved corrections!** with **Continue Editing**, **Discard**, **Save...**, and
**Contribute...**. Treat an in-progress edit with changed text as pending even
before it counts as a correction. Continue Editing restores the editor and its
draft. Discard clears all corrections and leaves Correction mode. From a quit
request, a successful discard, save, or contribution finishes quitting. The
exit dialog's Save and Contribute actions are available for a changed first
draft even while the main window still reads **Corrections: 0**.

| Action | Result |
| --- | --- |
| Save Corrections... or Save... | Write the TOML report locally and leave Correction mode after a successful save. |
| Contribute Corrections... or Contribute... | Open the prefilled GitHub issue form and leave Correction mode after the browser handoff. |
| Discard Corrections... | Clear all corrections and any draft, restore the original text, and stay in Correction mode. |
| Discard in the exit dialog | Clear all corrections and any draft, restore the original text, and leave Correction mode. |
| Continue Editing or dismiss the dialog | Keep all corrections and stay in Correction mode. |

When saving or contributing, include the open draft after validation. If the
file picker is cancelled, the write fails, or the browser cannot be opened, keep
the corrections and stay in Correction mode. Successful export clears the local
batch and restores the original display text. A clean exit skips the dialog.

Keep one language per correction batch so the report and issue title describe
the same language. If a language change would replace the language of a pending
batch, resolve the batch through the same dialog before applying the switch.
Continue Editing cancels the language switch. A cancelled or failed export
also keeps the current language and draft.

## Rendering and identity

The Qt canvas displays framebuffers produced by the shared firmware renderer.
Expose text hit regions from that renderer through `firmware/emulator/main.cpp`
and `cntrl/emulator/backend.py` while Correction mode is active. Each region
needs its display, pixel bounds, stable catalogue ID, full or short form,
effective language, and the font and layout information used to draw it. Tie the
regions to the corresponding frame so page changes cannot leave stale targets.
Carry the message ID from the point where the page requests the translation.
Matching rendered strings would confuse identical captions, and framebuffer
pixels alone cannot identify the source field. Use the image destination from
`display_rectangle` for pointer coordinates and overlay placement. A click in
the surrounding letterbox misses the text. Anchor an open draft to its message
even if a redraw moves the glyphs.

Keep text input and the two floating buttons in Qt. Use the shared bitmap text
renderer for the visible draft and the accepted preview. Apply temporary
overrides by catalogue ID and form in the emulated session, then let the usual
fit, wrap, and glyph fallback rules draw the result. Use the language builder's
existing validation rules where applicable and keep an invalid draft available
for revision. Correction previews live in memory and leave installed language
packs and tracked translation sources untouched.

Keep the page tooltip feature's page identity and translations independent of
correction hit regions. Suppress the tooltip over an active editor so it cannot
cover the edit controls.

## TOML report and GitHub handoff

Use the same UTF-8 TOML dump for Save and Contribute. Include the language tag,
language name, and one entry per changed catalogue field with its stable ID,
field (`text` or `short`), original text, and replacement. Take the original
from the pack displayed in this session, which can differ from the repository's
copy in a mirrored session. Sort entries by ID and field. One proposed
correction to the French catalogue would look like this.

```toml
language = "fr"
language_name = "Français"

[[corrections]]
message = "browse"
field = "text"
original = "parcourir"
replacement = "Parcourir"
```

Open the project's GitHub new-issue form in the user's browser with the title
`Translation correction for <language-name>`. Use the language name in the
report, giving `Translation correction for Français` in the example.
Attach the `.toml` if the handoff supports selecting a local attachment without
uploading it in advance. GitHub's documented URL prefill takes a title and body,
and its attachment interface uploads files immediately. Use a fenced TOML block
in the prefilled body for the standard browser handoff. If the body is too long
for URL prefill, tell the user the report will be copied for pasting, copy the
complete body, and open the form with the title filled in. Treat a failed copy
as a failed export. The user reviews the issue and decides whether to
submit it. [GitHub issue prefill](https://docs.github.com/en/issues/tracking-your-work-with-issues/using-issues/creating-an-issue#creating-an-issue-from-a-url-query)
and [GitHub attachments](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/attaching-files).

## Verification

- Check the three disabled buttons at zero, accepting and cancelling with both
  mouse and keyboard, recounting after re-edits and reversions, and restoring
  originals after discard.
- Check clicking and editing text at different display scales, across page
  changes, and with an alternate font or a short form. Confirm that authored
  text and live values have no correction targets and that tooltips still work.
- Use two IDs with identical visible text and one ID drawn on two pages to
  verify targeting, preview, and counts. Compare an untouched editor with the
  underlying pixels at native and resized scale, excluding its caret and outline.
- Cover every exit-dialog action, a cancelled save, a failed export, a pending
  draft on quit, and a language switch with pending corrections.
- Verify that the saved TOML and the issue body carry the same corrections,
  including accented text. Test the browser handoff with a stub and manually
  verify the prefilled form without submitting it.

## Additional work beyond the prompt

None.
