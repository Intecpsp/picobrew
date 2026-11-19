# Local web recovery candidate

The `feature/web-recovery` branch builds a small, independent replacement for
the old cloud recovery application in `/sys/mcuimg1.bin`. It contains its own
webpage, authenticated upload API, native filesystem adapter and SHA256
verification. It runs instead of the brewing application, with the four
heater/pump/valve control outputs driven LOW before networking.

**This is a separate software candidate, not an installed recovery system.**
The stable standalone 1.0.0 release remains on `feature/standalone-rest-api`.
No device file or boot record was changed during this work. The current guarded
USB installer intentionally cannot write image1 or switch into this candidate.
Do not select the original image1 expecting this webpage: that file is still
the cloud-dependent recovery program until a separately validated replacement
has actually been installed.

## What the candidate implements

- Inspect image2 and image3 and their actual logical `AllocatedLen` values.
- Load a firmware binary and its build report, plus the five matching gzip
  browser files when the report requires them.
- Build an immutable upload plan with exact lengths, SHA256 hashes and a
  session ID. Stale tabs cannot operate on a different plan.
- Upload bounded chunks, close each file, then reread every byte and verify
  its length, digest and image vectors or gzip header.
- Verify already matching files without rewriting them, including after a
  new browser/device session and a newly loaded plan.
- Separately choose a verified application as the normal boot destination,
  or try image2 once with recovery as the next unconfirmed-reset destination.
- Verify boot-record readback, finish the HTTP response, then restart.

Uploading never automatically selects or starts an application. A normal boot
selection and a trial boot each require a separate explicit confirmation.

### Filesystem boundaries

Application writes are limited to **already allocated** image2/image3 files.
The candidate cannot create, delete or move an application slot. It cannot
write its own recovery image, the secondary loader, API key, Wi-Fi credential
files, arbitrary paths or raw flash. It exposes no format operation.

The five application browser paths are fixed: `/local/web/i.gz`, `a.gz`,
`e.gz`, `h.gz`, and `s.gz`. Existing files are reused; absent browser files may
be created within fixed maxima of 7,680 / 7,680 / 3,584 / 7,680 / 3,584 bytes.
These correspond to the existing two/two/one/two/one-block allocation budget
after metadata. A larger future bundle requires a deliberate profile update.

Before a destructive open or payload write, both the portable core and native
adapter check that the logical boot record still selects recovery with normal
magic. A first application chunk must contain an aligned stack above the loaded
image, inside SRAM, and a complete Thumb reset instruction address inside the
image. Invalid headers fail before opening the existing file for write.

Reopening an application or browser file for write can erase its old contents.
A failed upload can therefore leave that target incomplete. Recovery remains
selected during normal upload/verification and its webpage is embedded in its
own executable, allowing another attempt without depending on those files.
Cleanup closes partial handles and invalidates verification; it never retries,
selects an application or deletes a file automatically. Thirty seconds without
upload progress expires an abandoned upload once the current HTTP request has
finished. Status polling does not extend that deadline. Closing the partial
file also permits protected AP fallback after a lost station connection.

The hashes detect corruption and bind files to the chosen report. They are not
a vendor signature or proof that arbitrary firmware is safe to execute. The
report's version is package metadata, not a version independently extracted
from an unknown application.

## Why the current Pico still has one main application

The owner's actual image3 is absent. The manager displays it as unavailable
and refuses to allocate it. Retaining all existing firmware images and their
allocations is intentional.

| Additional storage | 4 KiB blocks |
| --- | ---: |
| Another current 164,600-byte application, including allocation overhead | 41 |
| A future slot matching image2's current reservation | 48 |
| Another current application and separate matching browser bundle | 49 |
| Remaining headroom in the full standalone 1.0.0 feature budget | 5 |

Capacity is only one constraint. The original free extents were 21, 36 and 26
blocks; none can hold the current application contiguously. Relocating recovery
into another hole could create a larger contiguous area, but it changes the
approved layout and requires live preflight, migration and interruption tests.
It is not performed by this candidate.

Two application versions also share `/local/web/*.gz` today. Keeping two
application binaries would not by itself preserve two working browser bundles.
This candidate verifies the selected package's browser files before selecting
it; it does not claim automatic A/B rollback with independent assets. A future
dual-slot layout needs either versioned bundles or an explicit compatibility
contract, together with a smaller or more compact local-data budget.

The source calculations are in the [actual device layout](../analysis/device-flash-layout.json),
[standalone storage budget](release-1.0.0.md), and
[browser allocation manifest](../firmware/web/manifest.json).

## Boot selection and recovery entry

The audited secondary loader maps normal selector0 to image1, selector1 to
image2, and selector2 to image3. This candidate preserves boot-record bytes1–3
when changing its selector/magic. The loader's existing trial mechanism can
keep selector0 and try image2; a subsequent reset with its testing state
returns to image1. The candidate exposes this as a one-boot test.

**Standalone 1.0.0 does not confirm a trial and does not yet have a “restart into
recovery” command.** After a normal/default boot selection, entering recovery
again currently requires guarded USB boot-metadata programming. The UI states
this before a normal boot. A future main-firmware entry action must verify that
the new local recovery image is installed before selecting it; it must never
select the old cloud recovery program inadvertently.

The present secondary loader has no independent dial/RESET recovery override,
does not validate every selected-file read/vector, and does not automatically
reset a hung trial. Its trial/rollback helper also ignores metadata-write
failure. Consequently this work does not establish automatic crash recovery
or power-loss-safe boot switching. The original USB recovery route and exact
backup remain necessary while hardware acceptance and a stronger boot policy
are developed. See the [instruction-level boot audit](device-firmware-review.md).

## Network and authentication

The native entry first starts station mode with DHCP and existing station
profiles, allowing 20 seconds for association and a fresh IPv4 address. It
does not assume that the brewing application's saved SSID text files have
created a usable native station profile.

If station connectivity is unavailable, it configures the pinned SDK's
protected `SL_SEC_TYPE_WPA_WPA2` AP mode:

- SSID: `PicoRecovery-` plus the last six MAC-address hex digits.
- Address: `http://192.168.4.1/`, with an explicitly configured DHCP pool.
- AP password: the first 16 hex characters of the existing API key.
- Browser API authentication: the entire existing 64-character API key.

The AP is not started with a missing or invalid key. The password/key is not
printed in logs. The browser retains the key only in memory and sends it only
to the same origin. The page and all assets are embedded; there are no CDN,
cloud-service or AI dependencies. HTTP is intended for the chosen local
network or protected recovery AP; this candidate adds no TLS endpoint.

## API

All operations below require bearer authentication except health. Mutations
use JSON and exact field sets. Unknown/duplicate keys, escaped/non-ASCII values,
numeric overflow, oversized bodies and arbitrary paths are rejected.

| Method/path | Meaning |
| --- | --- |
| `GET /api/v1/health` | Public recovery identity and key-configuration status |
| `GET /api/v1/recovery` | Boot state, slot allocations, current plan and progress |
| `POST /api/v1/recovery/plan` | Validate/copy the package manifest; return session ID |
| `POST /api/v1/recovery/begin` | Prepare one planned target; no destructive open yet |
| `POST /api/v1/recovery/chunk` | Write the next exact offset, at most 1,024 raw bytes as hex |
| `POST /api/v1/recovery/finish` | Close and completely reread/verify that target |
| `POST /api/v1/recovery/verify` | Verify an existing target without rewriting |
| `POST /api/v1/recovery/abort` | Close/unverify the current partial upload |
| `POST /api/v1/recovery/select` | Reverify all planned files, commit/read back selection, then restart |

Every post-plan mutation requires the returned session ID. The target number
is 0 for the selected application and 1–5 for index/app/explorer/history/style.
No request accepts a device pathname. The bounded protocol reuses the existing
HTTP transport rather than buffering a whole application in RAM.

## Build and verification

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-test.txt -r requirements-web-test.txt
make bootstrap
make recovery-verify
python -m playwright install chromium
make recovery-web-test
```

The native build checks the vector table, SRAM layout, unchanged 32 KiB stack
and 32 KiB heap, undefined symbols, absence of brewing/control code and delete
services, and a conservative **57,344-byte** image limit. Exact final sizes and
hashes are in [`firmware/recovery/recovery-report.json`](../firmware/recovery/recovery-report.json).

Core and API tests cover destructive-open behavior, truncation, stale sessions,
read/write/close failures, maximum-size readback, changed files after earlier
verification, boot persistence faults and malformed requests. Native adapter
tests use simulated SimpleLink/GPIO services. Browser tests use the actual
portable recovery API/core with simulated files. The separate CI workflow also
rebuilds the stable application and checks its committed binary byte-for-byte.

Physical recovery installation, GPIO levels, station/AP radio behavior, native
flash writes, trial/normal boot, interrupted uploads and boot-record power-loss
behavior remain untested. A separately reviewed USB installer and hardware
acceptance sequence are required before deploying this candidate.
