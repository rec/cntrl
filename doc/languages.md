# UI languages

The pedal can use English or French. Language is a device-wide preference and
remains selected when you change setups. Patch names, filenames, MIDI messages
and authored text stay as written.

Open **Pages and tools**, select **Language**, browse the installed languages and
confirm your choice. The change applies without reloading the setup or stopping
playing, recording or looping. English is the default when no preference is set.

Install the supplied French pack over USB if it is not on the SD card:

```sh
cntrl language install fr --port PORT
cntrl global set language fr --port PORT
cntrl global clear language --port PORT
```

Installation also includes the English pack so the picker can display native
language names. `cntrl global list` shows the saved preference. Clearing it
selects English. Installing a replacement pack refreshes the selected language
without changing whether the preference was explicitly set.

Captions choose a bitmap size that fits the available window. Localized text
can wrap, use a reviewed short caption or end with an ellipsis when necessary;
it does not depend on scrolling or frame buffers. Built-in UI translation uses
its own font, preserving the fonts chosen for authored performance text.

Language packs contain translations and their font glyphs and live under
`/cntrl/languages` on SD. Only the active pack and a candidate being checked are
loaded into RAM. Failed reads, invalid packs or insufficient memory leave the
current language unchanged. If a selected pack is unavailable at boot, the
pedal uses its built-in English recovery UI while retaining the saved preference.
Reinstall the pack and select it again to restore that preference. The recovery
picker may display a locale tag when native-name glyphs are unavailable.

Backups retain installed packs and the separate device-global overlay. Setup
downloads include the selected pack when available; recovered emulator sessions
import it without modifying the setup source. A full restore of a selected
non-English language needs a valid matching pack. Packs are matched to the
firmware's message schema; install updated packs with updated firmware.

English and French are the supplied language choices for this implementation.
Locale tags and installed-pack discovery are not tied to menu positions. Other
scripts require the shaping and readability qualification described in
`plan/18n.md` before they can be supported.

To regenerate the shipped packs from the tracked catalogue and font sources:

```sh
uv run --no-sync python -m cntrl.localization.catalogue
uv run --no-sync python -m cntrl.localization.build
```

The catalogue uses stable semantic IDs and typed named placeholders. French
templates may reorder placeholders; they cannot add values or change types.
