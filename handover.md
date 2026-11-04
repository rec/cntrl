# Conversation handover

This file contains conversation-only context. Current behavior, configuration,
architecture and unfinished feature plans are intentionally left in their own
documents and source files.

## User working preferences not stored in the repository instructions

- Flash firmware changes unless the user explicitly says otherwise. This standing
  instruction does not require flashing documentation-only work.
- Work on main in this directory, with focused commits pushed individually.
  Fetch and rebase against origin/main before pushing; other workers may update it.
  Preserve other workers' uncommitted changes. Do not create merge commits.
- The user sometimes tests the pedal while an agent is working. Unexpected USB or
  MIDI observations can overlap those interactions; do not assume that explains
  every error or treat it as proof of a firmware defect.

## Last verified pedal state, 2026-11-02

These are historical bench observations, not a fresh check on 2026-11-04.

- The combined image built from `c833057` was uploaded through the running helper.
  Its SHA-256 was
  `9b275148c560644f447f29e0323fe32a518129d110313aa246b66f92d6289025`.
- The final live serial response was `CNTRL READY 25 22202 8c30355a`.
  `simple-setup` was selected, and all 22,202 downloaded bytes matched the newly
  compiled source. The verification also checked ten cycles, 59 paired patches
  and disabled animation color cycling.
- Both valid saved setups, `simple-setup` and `readability-demo`, were backed up,
  recompiled for format 25, restored and read-back verified. Before that upload,
  `font-demo`, `pman` and `sd-font-demo` were already reported invalid. They were
  deliberately preserved, rather than converted or removed automatically.
- That implementation passed 566 Python tests, native controller and hardware
  tests, the combined ARM build, Ruff, formatting, type checking and pyupgrade.
  The user has not yet reported physical results for the two-second throb delay.
- No explicit user result has arrived for the new C-on-Firmware-page entry and
  A+F return round trip. Earlier stock-switch qualification is already documented
  in the maker guide and should not be treated as testing this new entry path.
- The latest uniform-font Status layout was flashed and checked over serial, but
  no physical appearance confirmation arrived after that last layout revision.

## Temporary recovery material outside the repository

These paths existed when preparing this handover. Preserve them if recovery is
needed; they are not committed or guaranteed durable.

- `/private/tmp/cntrl-before-pulse-delay/`: pre-format-25 binaries, editable TOML,
  required font assets and `selected.txt` for the two then-valid setups.
- `/private/tmp/cntrl-pulse-delay-ready/`: recompiled format-25 binaries, editable
  exports and fonts used for the successful restore.
- `/private/tmp/cntrl-backup-pulse-delay.py`,
  `/private/tmp/cntrl-prepare-pulse-delay.py`, and
  `/private/tmp/cntrl-restore-pulse-delay.py`: scripts used for those transfers.
  Review their fixed paths before reuse; rerunning the backup could overwrite
  the earlier recovery material. The backup required elevated serial access
  after a sandboxed termios call returned Operation not permitted.
- `/private/tmp/cntrl-verify-simple-live.py`: the live byte-comparison script used
  after the final restore. Review its assumptions before running it again.
- `/private/tmp/cntrl-pulse-delay-pytest.log`,
  `/private/tmp/cntrl-pulse-delay-native.log`,
  `/private/tmp/cntrl-pulse-delay-core.log`, and
  `/private/tmp/cntrl-pulse-delay-build.log`: verification evidence from that work.

## Parked decisions and observations

- The user saw a pale blue/cyan background and a visible difference between the
  left and center panels. They suggested unequal backlight brightness, then
  explicitly said not to worry about it. Neither panel calibration nor a color
  channel swap was established as the cause. Do not reopen this without a reason.
- Lua support is postponed until there is a compelling use case. Repurposing
  ports for instrument-cable testing was also put aside.
- For future font choices, readability comes first and licensing must permit
  embedding. The user rejected Black Ops because of its military association.
  They also abandoned Atkinson Hyperlegible Next and Barlow Condensed after
  visible rendering ugliness; do not resume those trials unprompted.

No requested implementation remains in progress at this handover. The physical
confirmations above remain unreported, and the last hardware state should be
refreshed before another deployment. Documentation maintenance itself did not
access or flash the pedal.

## Publication blocker observed while writing this handover

The documentation-cleanup commit was pushed successfully. Subsequent fetch and
push requests for this handover failed with `Permission denied (publickey)`,
including outside the sandbox. This establishes an SSH authentication failure in
this session, not that the user's GitHub account credentials are generally invalid.
The handover commit remains local. Refresh SSH access, fetch/rebase and push it;
do not replace the SSH remote or force-push as a workaround.
