# UI languages

The pedal supports these Latin-script UI languages: English, French, German,
Spanish, Italian, European Portuguese (`pt-PT`), Dutch, Polish, Czech, Slovak,
Hungarian, Romanian, Swedish, Danish, Norwegian Bokmål (`nb`), Finnish and Turkish.
The new translations are complete drafts awaiting native-speaker review.
Language is a device-wide preference and
remains selected when you change setups. Patch names, filenames, MIDI messages
and authored text stay as written.

Open the **Language page** in the utility-page cycle, or select that page through
**Pages and tools**. It uses exactly two windows: the first is a scrolling list
of installed languages in their native names, with small country flags; the
second has the Language caption and controls. On the default three-display
layout these are the center and right windows. English uses the UK flag.

Use **B** to scroll up, **E** to scroll down, **C** to select and **F** for the
next page. Scrolling wraps at either end, and the initial highlight follows the
active language. Browsing alone does not change the UI language. The existing
reverse-page gesture still works. The old nested menu selector is removed.

The page is included in the default utility-page cycle immediately before None,
after Pong on targets with at least three displays. A one-display target omits
it by default and rejects an explicit
`type = 'language'` page; USB language commands remain available.
For explicit page catalogues, add a named page with `type = 'language'` and put
it in the desired region. The page always claims two windows, regardless of
`min_windows`, and uses only those two windows when more are available.

Selecting a language applies without reloading the setup or stopping
playing, recording or looping. English is the default when no preference is set.

Install the supplied French pack over USB if it is not on the SD card:

```sh
cntrl language install fr --port PORT
cntrl global set language fr --port PORT
cntrl global clear language --port PORT
```

Installation also includes the matching English pack. Native language names use
the shared flash font even in recovery. `cntrl global list` shows the saved preference. Clearing it
selects English. Installing a replacement pack refreshes the selected language
without changing whether the preference was explicitly set.

Captions choose a bitmap size that fits the available window. Localized text
can wrap, use a reviewed short caption or end with an ellipsis when necessary;
it does not depend on scrolling or frame buffers. Built-in UI translation uses
its own font, preserving the fonts chosen for authored performance text.

The default Tamzen Status/body font stays selected for captions it can render.
If any character in a caption is missing, the entire caption uses embedded Arimo
at the same size. This avoids mixing glyph styles or loading another font into
RAM. Tamzen includes the
139 base characters and every other supported drawable character from the
373-character comparison set, including the curly apostrophe. All supplied locales
reuse embedded Arimo and Tamzen glyphs, so their packs contain translations only.
Packs can still supply a language-specific font or size-13 Status supplement.
This uses language-pack format version 3. Install the supplied packs
with the updated firmware; earlier packs are rejected. If an old pack remains
on SD, the pedal uses its English recovery UI until the matching pack is installed.

Language packs contain translations and optional font glyphs and live under
`/cntrl/languages` on SD. Only the active pack and a candidate being checked are
loaded into RAM. Failed reads, invalid packs or insufficient memory leave the
current language unchanged. If a selected pack is unavailable at boot, the
pedal uses its built-in English recovery UI while retaining the saved preference.
Reinstall the pack and select it again to restore that preference. The recovery
page retains native language names using the shared flash font.

Backups retain installed packs and the separate device-global overlay. Setup
downloads include the selected pack when available; recovered emulator sessions
import it without modifying the setup source. A full restore of a selected
non-English language needs a valid matching pack. Packs are matched to the
firmware's message schema; install updated packs with updated firmware.

Install a locale by its tag, for example `cntrl language install de --port PORT`.
The additional tags are `de`, `es`, `it`, `pt-PT`, `nl`, `pl`, `cs`, `sk`, `hu`,
`ro`, `sv`, `da`, `nb`, `fi` and `tr`. English and French use `en` and `fr`.
Locale tags and installed-pack discovery are not tied to menu positions. Other
scripts require the shaping and readability qualification described in
`plan/18n.md` before they can be supported.

To regenerate the shipped packs from the tracked catalogue and font sources:

```sh
cntrl language build
```

The English reference is `cntrl/localization/messages.toml`; each additional
locale has a file in `cntrl/localization/translations/` with its native name and
all reference message IDs. The builder rejects missing or unknown IDs, invalid
placeholders and glyphs absent from the shared UI font. It preserves reference
message order in the binary, independently of translation file order.
Static captions longer than the 63-byte text field require a complete short
form within that UTF-8 byte limit. Error dialogs use that form when necessary,
wrap their text and preserve UTF-8 boundaries when shortening dynamic errors.

The catalogue uses stable semantic IDs and typed named placeholders. Translated
templates may reorder placeholders; they cannot add values or change types.
