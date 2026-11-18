# Standalone firmware 1.0.0

`pico-s-pro-standalone.bin` is the raw linked application image for the Pico
S/Pro reconstruction, starting at `0x20004000`. It contains the REST API,
persistent brew history, optional telemetry forwarding and browser-file
streaming. The matching five compressed browser files are packaged separately
in [`../web/`](../web/) and are required for the full browser console.

The image is **164,600 bytes**. Its SHA-256 is:

```text
652f94f941a4551ebb4a2df17e818dec3b3aebd2fc1cf43ec6bcb2e5ec684558
```

`firmware-report.json` records the source hashes, vectors, native link and RAM
checks, as well as the matching browser manifest. The original **32 KiB stack
and 32 KiB heap reservations remain intact**, with 952 bytes of unallocated
SRAM after the linked image and reservations. History and enabled telemetry
use reusable heap payloads totalling 4,704 bytes, plus allocator overhead,
within that heap. This is a link budget, not measured peak runtime usage.

The previous 161,720-byte standalone candidate was installed and booted on the
owner's Pico Pro, and its connected browser was observed. **This 1.0.0 candidate
has not yet been installed or physically exercised.** It is not a complete
serial-flash backup. Physical brew, interlock and restore validation remain
outstanding; see [hardware acceptance](../../docs/hardware-validation.md).

Use the [guarded USB installer](../../docs/macos-usb-installation.md). It accepts
the known previous candidate as an upgrade source, verifies the existing key,
installs and reads back the matching browser bundle, then rewrites and verifies
the existing image2 allocation. It retains all other firmware images and boot
metadata. No API key is included in these artifacts.

Rebuild with `make standalone`. The matching debug ELF, linker map and legacy
update-response envelope are generated in `build/standalone/firmware/`; the
CI workflow also uploads the raw image, ELF, map and report. The standalone
firmware does not implement an OTA receiver or an HTTP flashing endpoint.

See the [release notes](../../docs/release-1.0.0.md),
[API guide](../../docs/standalone-api.md), and
[optional telemetry guide](../../docs/telemetry.md).
