# Verified standalone storage

`VerifiedStorage` is the shared checked transaction behind the ESP32 and ESP8266
standalone storage ports. `FileIo` preserves a missing object separately from
an I/O failure, supplies partial transfer counts, and checks sync and close.
The SDK adapter owns and mounts the filesystem; this class never formats it.

Writes close and read back every byte of a temporary file before touching the
current file. They rename the old file to a backup, promote the temporary file,
read back the final name, and remove the backup only after that verification.
A failed promotion restores the backup when the main name is absent. Failed
final verification attempts to restore the previous file and reports an error.
An error after promotion can leave the verified new generation visible; callers
must not infer that an error means no filesystem state changed.

This is a recoverable transaction across several filesystem operations. It does
not claim power-failure atomicity for an individual SPIFFS metadata write. The
engine's versioned generations and CRCs remain necessary. Reads use a backup
only when the main object is missing. They do not replace a corrupt or unreadable
main object with its backup. Deletion removes the backup before the main object,
preventing stale data from reappearing after an interrupted deletion.

The copied namespace defaults to `/pb/`. Fixed engine keys are at most 23 ASCII
bytes; traversal, separators and reserved `.tmp`/`.bak` suffixes are rejected.
Including the four-byte prefix, transaction suffix and terminator, names fit the
32-byte SPIFFS limit. All operations are serialized by the foreground owner.

`analysis/verified-storage-validation.json` identifies the tested source and
reports every fault/recovery assertion. The deterministic model fails every
filesystem callback in replacement, first creation and removal, and restores
snapshots taken before and after mutation to test interrupted transactions.
It also checks partial transfers, corrupt readback, size mismatch, zero progress,
offset reads, descriptor closure and error-versus-missing behavior. The concrete
SDK drivers have separate target and actual SPIFFS validation.

```sh
python firmware/common/tools/validate_verified_storage.py --write-report
python -m unittest discover -s firmware/common/tests -p 'test_verified_storage.py' -v
```
