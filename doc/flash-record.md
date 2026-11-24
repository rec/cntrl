# Flash record

Each entry records one invocation of the firmware uploader. A retry gets its
own entry. An uploader exit of zero records upload success. An entry without
an outcome means the uploader's result was unavailable when logging stopped.

`cntrl flash` adds a reconnect check linked to its upload attempt. To count
cases where the uploader failed and the pedal booted other firmware, look for
a nonzero uploader exit and **Different firmware running**. **Expected build running** means the pedal
reported the requested build even if the updater returned an error. If the pedal
comes back in Morningstar stock mode, the reconnect check identifies that too.
HalfKay, a missing device, or an unreadable CNTRL status leaves the running
build unknown. Direct uploader commands do not perform a reconnect check.
Saved data is verified separately.

The three entries below were reconstructed from installation records and
observed terminal output. Their timestamps are approximate, in CET (UTC+01:00).

## 2026-11-22 14:27 CET (estimated)

- Image from `9bdcd1e`
- Image SHA-256 `0fc486a42d1aae25feaa538355a78f16da765d22aeb99409ce731309b5196686`
- Succeeded (uploader exit 0). The pedal booted and the installer verified the restored data.
- Reconnect: expected build running (`9bdcd1e`).

## 2026-11-24 14:22 CET (estimated)

- Image from `278f147`, with Doom
- Image SHA-256 `aa0abe2e9ff349c81ef7cdeedabaf5030e5987b9f1733fa813d7b4e823b6b384`
- Failed (uploader exit 1). The pedal remained in HalfKay after the attempt.
- Reconnect: CNTRL did not reconnect before timeout. Running build unknown.

      I/O error while writing to 'IOService:/…'
      pedal did not return after upload; installation evidence and backups retained

## 2026-11-24 14:23–14:42 CET (estimated)

- Retried the same `278f147` image
- Image SHA-256 `aa0abe2e9ff349c81ef7cdeedabaf5030e5987b9f1733fa813d7b4e823b6b384`
- Succeeded (uploader exit 0). The pedal booted, and a separate restore and read-back verification passed.
- Reconnect: expected build running (`278f147`), confirmed by a separate status check.
