# Standalone firmware project roadmap

## Goal

Reverse engineer the firmware for **Pico C, Pico S/Pro, Z-series and Zymatic**
so every supported machine offers the same standalone operating model, REST
API and browser experience as the custom S/Pro firmware. All implementation
work lives in
[picobrew-pico-pro-firmware](https://github.com/jeffreydwalter/picobrew-pico-pro-firmware).

Each port implements the shared public behavior through its own hardware
drivers, brewing controls and storage limits. The optional Hub presents these
machines through the same fleet, recipe and brew-history workflows.

## Current support

| Machine family | Standalone firmware | Live Hub operation |
| --- | --- | --- |
| Pico S/Pro | Implemented; installation and runtime evidence exists for earlier S/Pro builds on the owner's Pro. The packaged 1.1.0 release and a complete brew remain physically unverified. | Implemented through the S/Pro REST adapter |
| Pico C | Firmware reverse engineering and port planned | Recipe planning only |
| Z-series | Firmware reverse engineering and port planned | Recipe planning only |
| Zymatic | Firmware reverse engineering and port planned | Recipe planning only |

Recipe-planning profiles, archived machine programs and simulator results do
not establish live hardware support. The [firmware status](../README.md) and
[Hub coverage matrix](../hub/docs/coverage.md) document current limits.

## Shared behavior

The [S/Pro API guide](standalone-api.md) and [OpenAPI contract](openapi.json)
are the starting point for the common device contract:

- Local startup, installed recipes and brewing without a vendor cloud or Hub.
- A device-hosted browser interface, settings, recipe editing and API explorer.
- Versioned status, capabilities, recipes, commands, settings and retained
  brew-history resources, authenticated with a per-device key.
- Firmware-owned heating, pumping, timing, preparation prompts and stop
  behavior, with local and remote interfaces using the same machine state.
- Shared Hub recipe management, reviewed deployment and history collection,
  with model-specific capacities and supported operations exposed explicitly.

Model-specific implementation must preserve the shared workflows while
reporting the actual machine's sensors, storage and brewing capabilities.

## Work ahead

Continue the S/Pro validation work and recover the firmware behavior and
hardware interfaces of the other machine families. Implement those ports
against the common device contract, then verify their local browser and Hub
workflows on the corresponding hardware. Build/simulation results and physical
validation remain separate evidence.

The older community servers and firmware archives are read-only sources for
protocol and behavioral research. The intended live integration is each
machine's standalone firmware API. The historical Python Zymatic server is
not an installed component or a prerequisite for this architecture.
