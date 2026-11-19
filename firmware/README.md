# Firmware by machine

Each machine has `oem/`, `reverse-engineered/` and `modified/` locations.
Version-specific tools and tests live with their corresponding source tree;
general repository tools live at root `tools/`.

| Directory | OEM material | Source status |
| --- | --- | --- |
| [`pico-s-pro/`](pico-s-pro/README.md) | Verified device application image plus 1 upstream update envelope | Pre-v1 OEM reconstruction, modified 1.1.0 brewing firmware, and isolated experimental web recovery source populated |
| [`pico-c/`](pico-c/README.md) | 2 upstream update envelopes, including the alternate 0.1.34 image | Source placeholders |
| [`z1/`](z1/README.md) | See the separate Z-series archive; compatibility is not yet assigned | Source placeholders |
| [`z2/`](z2/README.md) | See the separate Z-series archive; compatibility is not yet assigned | Source placeholders |
| [`z-series/`](z-series/README.md) | 2 unchanged upstream Z-series images | Shared family archive; source placeholders |
| [`zymatic/`](zymatic/README.md) | None in the requested upstream directory | OEM and source placeholders |
| [`picoferm/`](picoferm/README.md) | 16 unchanged upstream images | Source placeholders |
| [`picostill/`](picostill/README.md) | 6 unchanged upstream images | Source placeholders |

## Complete upstream copy

All **27 files / 13,583,860 bytes** from
[`chiefwigms/picobrew_pico/app/firmware`](https://github.com/chiefwigms/picobrew_pico/tree/36acae3003845b72568ef5f7d6cf168acb6f9b69/app/firmware)
are included. [upstream-manifest.json](upstream-manifest.json) records every
original path, destination, Git blob identity, SHA-256, size and mode. No
version, alternate file or accessory family was omitted.

The S/Pro and C `.bin` files from that archive are ASCII OEM HTTP update
envelopes with hex payloads. They remain unchanged and are not presented as
raw device dumps. The separately verified S/Pro application image lives in
`pico-s-pro/oem/device/`.

Run `make verify-imports` at the repository root to check every copied file and
reconstruct the exact upstream Git subtree identity. The same check verifies
the complete pre-v1 S/Pro source snapshot. Original firmware packaging and
archive availability do not establish that an image can be flashed to another
model or that a custom port has been completed.
