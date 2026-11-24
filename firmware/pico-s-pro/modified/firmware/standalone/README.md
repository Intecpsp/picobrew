# Standalone firmware 1.0.0 packages

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
files are generated in the model-specific build output. The main firmware hands authenticated updates to the separate
`pico-hub-recovery.bin` application. See [OTA setup and verification](../../docs/hub-ota.md). Pico C, Z and
Zymatic cannot use these images merely by changing a label.

For first-time setup, use the Hub's **USB setup and recovery** flow and connect
the brewer to the Hub computer. The [USB installer](../../docs/macos-usb-installation.md)
compares the machine with its verified stock backup and flash layout before
writing. See the [hardware acceptance record](../../docs/hardware-validation.md)
for the remaining physical checks.

See the [API guide](../../docs/standalone-api.md) and
[Hub connection guide](../../docs/telemetry.md).
