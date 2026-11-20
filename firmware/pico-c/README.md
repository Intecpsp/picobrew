# Pico C

The complete upstream images for this family are in [`oem/upstream/`](oem/upstream/).
They retain their source filenames and bytes; see the repository-wide
[import manifest](../upstream-manifest.json).

[`reverse-engineered/`](reverse-engineered/) contains the typed OEM application
reconstruction and complete native CC3200 builds for the SSD1325 and SSD1309
display variants. Its documentation separates original-instruction verification,
bounded-memory repairs, and hardware qualification. No physical installation or
brewing validation is claimed.

[`modified/`](modified/README.md) implements standalone firmware 1.2.0 with the
local API, browser, recipes, history, Hub-managed live readings and cancellation
interlocks on the recovered C application. Both SSD1325 and SSD1309 native
images build; software validation does not establish physical installation or
brewing. Firmware package management is implemented; in-place installation is not.

The build/API identity is `pico-c` / **Pico C**, in family `pico-c`.
Firmware follows the shared [model identity contract](../../docs/machine-identity.md).
Build through the root router with `MODEL=pico-c` and an explicit
`DISPLAY_VARIANT=ssd1325` or `DISPLAY_VARIANT=ssd1309`. The compiled model/display
identity does not detect hardware or qualify the wrong initial flash.
