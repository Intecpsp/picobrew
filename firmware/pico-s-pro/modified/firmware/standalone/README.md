# Standalone firmware 1.2.0 packages

The two packages use the shared S/Pro implementation with an explicit model
selection. Install the image matching the physical appliance:

| Model | Application | Build output |
| --- | --- | --- |
| Pico Pro | `pico-pro/pico-pro-standalone.bin` | `build/standalone/pico-pro/firmware/` |
| Pico S | `pico-s/pico-s-standalone.bin` | `build/standalone/pico-s/firmware/` |

Each model directory contains its own `firmware-report.json`, recording model,
version, source hashes, binary hash, complete link checks and memory layout.
Both raw applications start at `0x20004000`. They are not complete serial-flash
backups and contain no API key. The matching five compressed browser files in
[`../web/`](../web/) are shared and required for the complete browser console.

Build with `make standalone MODEL=pico-pro` or `make standalone MODEL=pico-s`.
The default is Pro. Matching debug ELF, map and legacy update-response envelope
files are generated in the model-specific build output. The standalone firmware
does not implement an OTA receiver or HTTP flashing endpoint. Pico C, Z and
Zymatic cannot use these images merely by changing a label.

The [guarded USB installer](../../docs/macos-usb-installation.md) remains tied to
a pinned Pro backup and allocation layout. A separately named S image does not
qualify another appliance for that installer. Preserve and verify each appliance's own backup and installation
requirements. Physical brew and interlock validation remain in the
[hardware acceptance record](../../docs/hardware-validation.md).

See [release 1.2.0](../../docs/release-1.2.0.md), the
[API guide](../../docs/standalone-api.md), and
[Hub connection guide](../../docs/telemetry.md).
