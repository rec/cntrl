# Portable endpoint records and saved setups

Setup format 35 adds a required `CEND` section after `CMOV` and before `CNAM`.
The header is four bytes `CEND` followed by a little-endian uint16 record count.
Each 68-byte record contains:

| Bytes | Meaning |
| --- | --- |
| 0 | Logical route ID |
| 1 | Transport: 0 physical, 1 USB device, 2 USB host |
| 2 | Flags: bit 0 selectable recording input, bit 1 selectable operation output |
| 3 | Configurable-port slot, or 255 for a fixed connection |
| 4..35 | Null-padded ASCII name |
| 36..67 | Null-padded ASCII display label |

Names identify endpoints independently of transport, connector type or route
order. Route IDs form a dense permutation of 0 through record count minus one.
The existing 16-bit masks limit the wire format to sixteen routes; configurable
roles and expression assignments retain four wire slots. A connection can serve
an internal forwarding queue without being a selectable operation output.

A target supplies `midi_endpoints` and `default_midi_output`. `Target.outputs` is
a computed list of endpoint names with their output capability enabled. Omitted
playback outputs resolve to that target default; omitted recording inputs select
all declared recording inputs. Configurable-port names bind through each
endpoint's explicit `port` slot.

The adapter supplies `Hardware::midi_outputs()` and constructs `midi::Bridge`
with that descriptor view. It owns USB packet decoding, cable-to-route mapping
and physical writes. Generic routing allocates one queue per declared route,
without a fixed physical/USB route split. Descriptor strings and the descriptor
table must outlive their consumers.

`migrate.upgrade_setup` converts format 34 to 35 without recompiling musical
graph nodes or cached edited values. The caller supplies a target descriptor and
an old-to-new movie checksum map. It converts the layout section, inserts the
endpoint table, updates movie references and their editable source paths, and
updates size/checksum fields. Referenced movies missing from the map are errors.
`legacy.upgrade` first normalizes formats 20 through 33 into format 34.

`migrate.upgrade_movies` validates saved checksums and converts legacy animation
assets using explicit geometry. Its map includes identity entries for assets
already in the current format. `migrate.upgrade_songs` preserves the ordered
song/setlist records and upgrades each embedded setup independently. History
revisions use the same setup conversion while preserving their number, label
and modification time; their manifest length and version must be updated.
Firmware and ordinary setup loading accept only the current setup format.
