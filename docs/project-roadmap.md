# Standalone firmware project roadmap

## Goal and layout

Reverse engineer the firmware for **Pico C, Pico S/Pro, Z-series and Zymatic**
so every supported machine offers the same standalone operating model, REST
API and browser experience as the custom S/Pro firmware. All work lives in
[picobrew-pico-pro-firmware](https://github.com/jeffreydwalter/picobrew-pico-pro-firmware).

Each machine has `firmware/<machine>/{oem,reverse-engineered,modified}`:
original images, the preserved OEM reconstruction, and our custom firmware.
The complete upstream archive also includes PicoFerm and PicoStill images.
The shared Hub is self-contained in [`server/`](../server/README.md), with
general repository tools at root `tools/`.

Each firmware port implements the common public behavior through its own
hardware drivers, brewing controls and storage limits. The optional Hub uses
the same fleet, recipe and brew-history workflows across supported machines.

## Current support

| Machine or archive | Firmware state | Live Hub operation |
| --- | --- | --- |
| Pico S/Pro | Verified original application image; exact 396-file pre-v1.x reconstruction; modified 1.1.0 firmware implemented. Earlier builds have device installation/runtime evidence; 1.1.0 installation and a complete brew remain physically unverified. | Implemented through the S/Pro REST adapter |
| Pico C | Both upstream 0.1.34 images preserved; OEM C reconstruction and standalone 1.2.0 source builds for explicit SSD1325/SSD1309 variants are implemented. Native/host software tests pass; physical installation, brewing and OTA recovery qualification remain outstanding. | C API adapter, model/variant discovery, telemetry, geometry-aware display and variant-scoped package management implemented |
| Z1 and Z2 | Separate development directories; reconstruction and custom firmware unimplemented | Generic Z planning profile only; no model-specific live adapter |
| Z-series archive | Two upstream images preserved in their original shared family grouping; compatibility with individual Z models unverified | No archive-derived live support |
| Zymatic | Reconstruction and custom firmware unimplemented; no image in the pinned upstream archive | Recipe planning only |
| PicoFerm | All 16 upstream images preserved; reconstruction/custom implementation absent | Unimplemented |
| PicoStill | All six upstream images preserved; reconstruction/custom implementation absent | Unimplemented |

Recipe-planning profiles, archived programs and simulator results do not
establish live support. See the [S/Pro firmware status](../firmware/pico-s-pro/modified/README.md),
[Hub coverage matrix](../server/docs/coverage.md),
[OEM source manifest](../firmware/pico-s-pro/source-manifest.json) and
[complete upstream manifest](../firmware/upstream-manifest.json).

The independent [S/Pro web recovery candidate](../firmware/pico-s-pro/modified/recovery/README.md)
is preserved alongside the modified brewing firmware. Its image1 source/package
and host/build verification are available, but its installation, entry path,
trial confirmation and physical recovery remain unimplemented or unvalidated.

## Shared behavior

The [S/Pro API guide](../firmware/pico-s-pro/modified/docs/standalone-api.md)
and [OpenAPI contract](../firmware/pico-s-pro/modified/docs/openapi.json) are
the starting point for the common device contract:

- Local startup, installed recipes and brewing without a vendor cloud or Hub.
- A device-hosted browser interface, settings, recipe editing and API explorer.
- Versioned status, capabilities, recipes, commands, settings and retained
  brew-history resources, authenticated with a per-device key.
- Firmware-owned heating, pumping, timing, preparation prompts and stop
  behavior, with local and remote interfaces using the same machine state.
- Shared Hub recipe management, reviewed deployment and history collection,
  with model-specific capacities and supported operations exposed explicitly.

Each port must preserve these workflows while reporting its actual sensors,
storage and brewing capabilities.

## Work ahead

Continue S/Pro validation and recover the firmware behavior and hardware
interfaces of the other target machines. Keep original images and exact
reconstruction baselines separate from work in each `modified/` directory.
Implement ports against the common device contract, then verify their local
browser and Hub workflows on corresponding hardware. Source/archive coverage,
build/simulation results and physical validation are separate evidence.

Older community servers and archives are read-only research sources. Live
integration uses each machine's standalone firmware API. The historical Python
Zymatic server is not an installed component or prerequisite for this design.
