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
| Pico S/Pro | Verified original application image; exact 396-file pre-v1.x reconstruction; modified 1.2.0 firmware implemented. Earlier builds have device installation/runtime evidence; current model-specific images and a complete brew remain physically unverified. | Implemented through the S/Pro REST adapter |
| Pico C | Both upstream 0.1.34 images preserved; OEM C reconstruction and standalone 1.2.0 source builds for explicit SSD1325/SSD1309 variants are implemented. Native/host software tests pass; physical installation, brewing and OTA recovery qualification remain outstanding. | C API adapter, model/variant discovery, telemetry, geometry-aware display and variant-scoped package management implemented |
| Z1 and Z2 | Physical mappings remain unassigned; shared recovered/modified implementation is under z-series | Generic family identity only; no model-specific compatibility claim |
| Z-series | Two originals preserved; recovered Core/Head SDK application and modified standalone reference image implement local control, persistent recipes/history and explicit restart recovery | Authenticated discovery, canonical recipe deployment, commands, grayscale display, settings, telemetry, recovery and history |
| Zymatic | 1.1.14 controls reconstructed and standalone AVR extensions simulated | 1.2.0 conservative USB layout. 1.3.0 adds Ethernet panel control, owner pairing and EEPROM session history on verified boot layouts. Physical trials pending. |
| PicoFerm | All 16 originals preserved; recovered SDK application and modified bounded acquisition/calibration/recording runtime implemented | Named sensors, explicit recording, calibration, cadence, history and telemetry; no brewer controls |
| PicoStill | All six originals preserved; recovered SDK application and modified native accessory controller implemented | Native modes, pump/two fan channels, named sensors/actuators, history and telemetry; no heater/recipe claim |

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

The [firmware parity matrix](firmware-parity.md) records which workflows are
shared and which controls depend on the actual machine hardware.

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

Keep originals and frozen reconstruction baselines unchanged, and retain every
remaining original-execution limitation in the component evidence. Software
source/image builds and the shared local API/browser/Hub are implemented for
the available S/Pro, C, generic Z and accessory targets. Physical qualification
still requires matching device backups/layouts, real sensor/output tests,
heap/stack timing measurements, controlled restart tests and operating trials.
Zymatic has its own original AVR application and requires trials on its own hardware. The separate S/Pro recovery
candidate and C OTA/bootstrap remain outside the verified installation scope.

Older community servers and archives are read-only research sources. Live
integration uses each machine's standalone firmware API. The historical Python
Zymatic server is not an installed component or prerequisite for this design.
