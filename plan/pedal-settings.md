# On-pedal Settings

Settings covers adjustments that are useful during setup or at a rehearsal.
Configure layouts, MIDI routing, styles, and patch actions in the desktop setup
and song library files. The firmware still loads previously saved settings,
including song and setlist appearance.

## Pedal controls

- Set the startup set from **Pedal** or use the current set at startup.
- Adjust device-wide brightness, change language or boot firmware, and edit
  Contact. Calibrate expression pedals and adjust their calibration options.
- Open **Setup** for the loaded setup's brightness. Press D from **Setup** to
  choose an instrument and adjust its MIDI channel.
- Open **Setups** to choose a saved setup or work with its revision history.
- Open **Songs** to rename a song or reorder or remove its steps.
- Open **Setlists** to add, move, or remove a song occurrence and rename the
  setlist. A rename updates the startup set selection when applicable.

See [Pages](../doc/pages.md) for the controls and save prompts. The
[settings inventory](all-settings.md) marks values that are editable on the pedal.

## Verification

- Change or clear each brightness override. Verify that setup brightness takes
  precedence and a device-only change leaves the setup file intact.
- Adjust one instrument's channel. Check that the output and other instruments
  stay unchanged and that every program using that instrument follows the edit.
- Reorder a repeated song occurrence during performance and save. Verify that
  no MIDI is sent and the continuation still points to the right occurrence.
- Rename the startup set and verify recovery after an interrupted save.
- Load an existing compiled setup and song library and check their authored
  routing, layout, and appearance after saving an unrelated pedal adjustment.

## Additional work beyond the prompt

None.
