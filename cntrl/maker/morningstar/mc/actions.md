# Actions module on MC6 Pro

The module implements the portable
[momentary/latched action specification](../../../../plan/momentary-latched.md).
See [configuration and controls](../../../../doc/momentary-latched.md).
Open A+D, choose Actions, then use the configured A through E assignments. F begins
cleanup; a second F explicitly abandons blocked cleanup with an unknown-state
warning. Held entry switches rearm individually when released.

`ACTIONS=1` is the default firmware build setting. `ACTIONS=0` removes the module,
menu entry and owned-message storage. A host target profile with `max_actions=0`
rejects configurations requiring the excluded module. The target adapter pauses
the performance bridge while Actions is open and restores the setup preference
after cleanup. Setup/asset replacement and stock-firmware requests are rejected
until the module closes. Returning to the module menu performs cleanup before
Panic or Original firmware can be entered.

## Deployment: 2026-10-28

The combined image was built after rebasing onto the incoming patch-function,
Unicode-font, cooperative song-job and cooperative setup-browsing changes,
including the song bounds-check fix. Image SHA-256:

```text
2e0910558d020c76f9aa7eea04483f3c17911c1bfce271f485e3303c5c94121d
```

The running upload helper installed the image on pedal serial 14468280. The
updater's initial write failed; after verifying HalfKay identity, retrying the
same image reached Sending reset and the application re-enumerated as
`cntrl experimental`. Reset/disappearance exit status was checked against actual
USB enumeration and application replies rather than treated as success alone.
Host serial settings were normalized to 115200 for post-flash verification.

Before flashing, all three setups were downloaded as editable TOML and exact
binary copies, and saved as on-pedal history snapshots. The empty song library
was also downloaded. Backups are at
`/private/tmp/cntrl-actions-device-backup`. The setups were recompiled for format
16 and restored with verified font assets. The incoming sparse-font implementation
required rebuilding the font demos from the repository's original font outlines;
the original downloaded font files and setup bytes remain in the backup.

Post-flash exact read-back verification:

| Setup | Compiled bytes |
| --- | ---: |
| font-demo | 1496 |
| pman | 34614 |
| sd-font-demo | 1490 |

The empty song library was restored and verified. `pman` remains selected, with
status `CNTRL READY 16 34614 d630730a`. Existing setups have no action assignments;
installation does not add MIDI commands to them.

The included build reports 55,808 bytes free for local variables and 305,376
bytes for heap allocation. Included/excluded builds, portable sanitizer tests,
target native tests, 369 Python tests, Ruff, type checking and pyupgrade checks
passed. The independent rearming regression was added after integration review
and passed with the native suite before the final build.

Physical switch operation, latency under load and external MIDI reception for
the Actions module remain to be qualified on the pedal. Installation and serial
read-back establish deployment and data integrity, not those physical behaviors.
