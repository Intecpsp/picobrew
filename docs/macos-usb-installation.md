# Guarded Pico Pro writes from macOS

The owner has verified a complete 1 MiB backup and a live logical-file
preflight using the existing SparkFun CH340C and direct USB on macOS.
The live boot record is `01 3d 00 20 ba dc cd ab`, selecting
`/sys/mcuimg2.bin`. The [device review](device-firmware-review.md) records
the firmware identities and allocation constraints.

`tools/pico_flash_install.py` provides three separate operations. **Physical
file writes, stock restoration and standalone boot have not yet been
validated. The next hardware step is Stage 1 only.** A successful key
write exercises the normal file-write protocol; it does not demonstrate
recovery from a power interruption during an application write.

| Command | Files it may write | Purpose |
| --- | --- | --- |
| `provision-key` | `/local/api.key` | Create/read back the required key, then rewrite/read back the same key using existing-file mode |
| `install` | `/local/api.key`, `/sys/mcuimg2.bin` | Verify the matching key through existing-file mode, then install and read back the pinned candidate |
| `restore` | `/sys/mcuimg2.bin` | Restore and read back the exact stock application extracted from the verified original backup |

The writer never writes the secondary bootloader, recovery image, boot
record, service pack or Wi-Fi configuration. It does not format flash,
delete files, move allocations, perform raw flash writes, or automatically
start the application. A saved preflight report cannot bypass its current
session checks.

## Required local files

This tool is deliberately restricted to the owner's reviewed device
profile. The original backup must be exactly 1,048,576 bytes with SHA-256:

```text
837a728400da02b9da4a7d563e1b67fb24d6a5af03c054963c4b1de60c72343f
```

The existing backup path is:

```text
~/Code/pico-pro-source/firmware/backups/pico-pro-serialflash-20261117-220719.bin
```

Every mutation command verifies that entire local backup before opening
USB. It extracts and verifies the original image 2 locally; the backup
and its Wi-Fi credentials are never uploaded anywhere by this tool.
Keep the backup and its original checksum/metadata files unchanged.

Use the existing repository environment. If recreating it is necessary:

```sh
PICO_ROOT="$HOME/Code/pico-pro-source"
cd "$PICO_ROOT"
python3 -m venv .venv
"$PICO_ROOT/.venv/bin/python" -m pip install -r requirements-usb.txt
mkdir -p "$PICO_ROOT/firmware/backups"
```

The writer imports the repository's probe, queued-backup, preflight and
offline-inspection modules. Run it from the checkout; copying only the
writer into Downloads is insufficient. The proven transport modules are
unchanged.

## Stage 1: provision and verify the API key

Update the existing feature-branch checkout and create the local key only
if it does not already exist:

```sh
PICO_ROOT="$HOME/Code/pico-pro-source"
cd "$PICO_ROOT"
git pull --ff-only origin feature/standalone-rest-api

if [ ! -e "$PICO_ROOT/private/api.key" ]; then
    "$PICO_ROOT/.venv/bin/python" \
        "$PICO_ROOT/tools/provision_api_key.py" \
        --output "$PICO_ROOT/private/api.key"
fi
```

The key generator writes exactly 64 lowercase hexadecimal characters plus
LF with mode `0600`. The writer requires that format in a private regular
file and never prints its contents, hash or file-access token. An existing
matching device key is reused and verified; a different key causes an
abort. Do not generate a new key to work around a mismatch.

Turn the Pico off and disconnect its mains power, leaving USB connected.
Run:

```sh
PICO_ROOT="$HOME/Code/pico-pro-source"
PICO_BACKUP="$PICO_ROOT/firmware/backups/pico-pro-serialflash-20261117-220719.bin"
PICO_REPORT="$PICO_ROOT/firmware/backups/pico-pro-key-$(date +%Y%m%d-%H%M%S).json"

sudo "$PICO_ROOT/.venv/bin/python" \
    "$PICO_ROOT/tools/pico_flash_install.py" provision-key \
    --backup "$PICO_BACKUP" \
    --key-file "$PICO_ROOT/private/api.key" \
    --output "$PICO_REPORT"
```

On **`BREAK asserted`**, reconnect the Pico's mains and turn it on. Do not
press the blue RESET button or wait for a Return prompt. This is the same
sequence that succeeded for the backup and preflight.

The tool checks the live protected images, original application, boot
record, flash geometry and allocation metadata. If the key is absent,
it also requires unambiguous absence from validated FAT/name metadata
before creating it. A logical “absent” flag by itself is insufficient.
It then verifies the key, reopens it for existing-file write, rewrites
the identical bytes and verifies them again. Before reporting success,
it rereads the original application and protected state.

Expected final marker:

```text
SUCCESS: API key provisioning and existing-file write verification completed
```

Return this run's terminal output for review before proceeding to Stage 2.
The original application remains selected throughout Stage 1. After the
tool exits, power off and disconnect mains before disconnecting USB; normal
power-on with USB disconnected should still run the original application.

## Stage 2: install the pinned standalone candidate

Proceed after Stage 1 has physically passed. The candidate in
`firmware/standalone/pico-s-pro-standalone.bin` is exactly **161,720 bytes**,
SHA-256:

```text
a8fc16afce43c4b91b82374a339b86631daefe1a40f08df5f8bdd7a0d0a8d904
```

The tool pins that content, the original backup and the current device
profile. It accepts the original application or this exact candidate as
the current image 2. Unknown or partial image contents require the restore
path; they are not overwritten by a normal install. A different build,
even from this branch, needs a reviewed tool/profile update.

With the same power sequence:

```sh
PICO_ROOT="$HOME/Code/pico-pro-source"
PICO_REPORT="$PICO_ROOT/firmware/backups/pico-pro-install-$(date +%Y%m%d-%H%M%S).json"

sudo "$PICO_ROOT/.venv/bin/python" \
    "$PICO_ROOT/tools/pico_flash_install.py" install \
    --backup "$PICO_ROOT/firmware/backups/pico-pro-serialflash-20261117-220719.bin" \
    --key-file "$PICO_ROOT/private/api.key" \
    --firmware "$PICO_ROOT/firmware/standalone/pico-s-pro-standalone.bin" \
    --output "$PICO_REPORT"
```

The writer first proves existing-file writing on the matching key in this
session. It then opens the existing image 2 with mode `0x1000`, retaining
its allocation and attributes, and writes sequentially. Opening for write
**erases the file's contents immediately**. It is not an atomic update.
Every write requires an acknowledged command and successful loader status;
the tool does not retry an ambiguous write chunk.

After closing, it rereads every application byte, compares with the local
candidate, verifies the key, and rechecks the protected state. If the
candidate was already installed, it skips the application rewrite while
still verifying the key and final state. Expected marker:

```text
SUCCESS: standalone firmware and API key verified
```

The tool leaves the Pico in the programming session. First standalone
startup, display/controls, network access and output behavior are separate
physical validation steps described in [hardware validation](hardware-validation.md).

## Restore the original application

The stock restore source is the verified full backup, not a freshly
downloaded image or the cloud-dependent recovery application. The writer
extracts exactly 162,792 original bytes with SHA-256
`e18c240fa1cf35222674bf3b4fa992a306c6e63b0b99be8793604f199085d702`.

Start a new programming session with the same power sequence:

```sh
PICO_ROOT="$HOME/Code/pico-pro-source"
PICO_REPORT="$PICO_ROOT/firmware/backups/pico-pro-restore-$(date +%Y%m%d-%H%M%S).json"

sudo "$PICO_ROOT/.venv/bin/python" \
    "$PICO_ROOT/tools/pico_flash_install.py" restore \
    --backup "$PICO_ROOT/firmware/backups/pico-pro-serialflash-20261117-220719.bin" \
    --output "$PICO_REPORT"
```

Restore checks the protected images, logical boot record and named image 2
allocation. It does not require the damaged application's contents to be
readable or hash-valid. It attempts only an existing-file write, then
requires complete stock readback and protected-state checks. It leaves
the API key and all other files alone. Expected marker:

```text
SUCCESS: original firmware restored and read back exactly
```

If the filesystem cannot reopen the existing image 2, restore stops.
The loader's coarse file-info “absent” result cannot distinguish every
corrupt/interrupted state; it does not authorize deletion, recreation,
formatting or raw writes. **This bounded restore command has not yet been
tested on the physical Pico, and cannot guarantee recovery from every
interrupted write.** Do not use Factory Reset as an offline restore method.

## Reports and failures

Each command requires a new absolute `--output` path. It reserves a
private report and an append-only journal before opening USB, records
intent before destructive opens, and records completed operations after
acknowledgment/status checks. The report and adjacent `.journal.jsonl`
contain no key contents, key digest or file-access token. Failed runs
retain their evidence; files are never silently overwritten.

On any transport failure, unexpected response, different protected hash
or readback mismatch, the tool stops without automatically booting or
resending an uncertain write. Keep the terminal output and report. After
an application-write failure, remain in the programming workflow until
the application has been successfully verified or restored.

The original backup, probe and preflight remain usable independently.
`make usb-test` runs synthetic device/protocol tests without physical USB;
those tests include immediate erase on existing-write open and failures
after a command may already have taken effect.

### APPS handshake diagnostics

The owner's first `provision-key` attempt on 2026-11-17 at 23:33:53
verified the local backup and initial ROM version, then stopped at the
second `APPS INITIAL ACK` handshake. It did not reach helper upload or
any filesystem operation. The [sanitized attempt record](../analysis/device-key-attempt-20261117.json)
preserves that distinction. Key provisioning is still physically unverified.

Revision 1 replaced the underlying error with `ProbeError: operation
failed; device payload omitted`, which concealed whether the device was
silent, returned NACK, or reached the unexpected-byte limit. That output
does not establish a baud, wiring or timing cause.

Revision 2 keeps the same inherited BREAK sequence and adds safe failure
diagnostics. A timeout with no bytes, for example, is reported as:

```text
STOPPED during RAM loader setup: APPS INITIAL ACK: timed out; received 0 bytes; wait limit 5s; elapsed ...s
No Pico flash or file write was attempted
```

This is an example of the new reporting, not a recovered explanation of
the earlier failure. The report's `error.handshake` field distinguishes
timeout, NACK and the 1,024-byte bound without storing received payload.
It also records `helper_uploaded_bytes`, `helper_executed` and
`persistent_write_attempted`. No automatic retry or connection-setting
change was added. Use a new report path and the same host key for a fresh
attempt; the prior report and journal remain evidence.

## Protocol references

The wire framing and helper come from the pinned
[`cc3200tool` revision](https://github.com/toniebox-reverse-engineering/cc3200tool/blob/c2332898a1ededc0bdd01ec279b23366d213f81e/cc3200tool/cc.py).
The writer does not call its generic `write_file()` helper, which deletes
and recreates existing files.
TI's [SWRU368C, chapter 13](https://www.ti.com/lit/ug/swru368c/swru368c.pdf)
documents whole-file erase on open-for-write and fixed allocation.
The [CC3100/CC3200 filesystem API](https://software-dl.ti.com/ecs/cc31xx/APIs/public/cc31xx_simplelink/latest/html/group___file_system.html)
defines distinct read, existing-write and creation modes. See
[SWRA657](https://www.ti.com/lit/an/swra657/swra657.pdf) for allocation and
serial-flash power-interruption limitations.
