# PicoStill

The complete upstream images for this family are in [`oem/upstream/`](oem/upstream/).
They retain their source filenames and bytes; see the repository-wide
[import manifest](../upstream-manifest.json).

[`reverse-engineered/`](reverse-engineered/README.md) contains the recovered
application, controller, physical inputs/outputs, sensor acquisition,
calibration, network/server/storage behavior and diagnostic paths, with
original instruction replay and a complete pinned ESP32 SDK link. The six
preserved releases are application containers with verified segment checksums
and appended SHA-256.

[`modified/`](modified/README.md) implements standalone 1.2.0 with explicit
native accessory modes, pump and two fan channels, sensor interlocks, physical
controls, persistent history, verified UART provisioning and the authenticated
common API/browser/Hub connection. It reports its own temperature/pressure/fan
capabilities and does not advertise an external brewer's heater or recipes.

Build with `make firmware MODEL=picostill`. The reference package uses an
explicit custom ESP32 layout and prepared filesystem. Source comparisons,
SDK linking and package verification are software evidence; initial-flash
compatibility, physical calibration and operation remain unqualified.
