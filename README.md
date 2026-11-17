# PicoBrew Pico S/Pro local control firmware

The `feature/standalone-rest-api` branch adds a **standalone firmware profile**
with an on-device REST API, browser console, recipe collection, settings and
local session identifiers. It boots without a PicoBrew account, cloud recipe
server, registration request or firmware-update service. The browser assets
are embedded in the application; no companion server or CDN is required.

**This is an experimental firmware candidate. It has not run on a physical
Pico Pro or Pico S.** Build, host simulation and ARM instruction tests do not
establish electrical I/O, radio/flash timing, installation/recovery, or a
successful brew on an appliance.

Start with the [standalone guide](docs/standalone-api.md), the
[OpenAPI contract](docs/openapi.json), and the separate
[device-key provisioning procedure](docs/standalone-provisioning.md).
`make` now selects the standalone build on this branch. The preserved legacy
profile remains available through `make firmware` and `make verify`.

## Standalone features

| Capability | Implementation |
| --- | --- |
| Local operation | Cloud registration, recipe/session requests, reporting and updates replaced or disabled at compile time |
| REST API | Versioned `/api/v1` resources for status, recipes, settings, recent events and commands |
| Remote front panel | Live 128 × 64 OLED image plus encoder turn/click controls for existing prompts and menus |
| Programs | Local recipes, manual brewing, sous vide, cold brew, rinse, descale, circulation, draining and racking |
| Stop | Latched output inhibition, cooperative cancellation and idle-only clear; filter removal during interrupt-side pumping also latches a stop |
| Recipes | 16 on-device recipes, up to 15 steps each, with validated input and CRC-protected alternating records |
| Browser console | Same-origin HTML/CSS/JavaScript, recipe editing/import/export, telemetry, controls and display preferences |
| Authentication | Per-device bearer key in `/local/api.key`; no shared/default credential or embedded secret |

PicoStill, dedicated pause/resume REST commands, remote firmware updates and
durable brew-history storage are not implemented in this profile. Pause/resume
uses the existing front-panel workflow. The activity feed contains the latest
eight events from the current boot. Fatal firmware halts can still require a
physical reset. See the guide for the exact operating and validation limits.

### Build and test standalone

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-test.txt
make bootstrap
make standalone-verify
```

The native image and its report are written under `build/standalone/firmware/`:
`pico-s-pro-standalone.bin`, matching ELF/map, and `firmware-report.json`.
The build checks vectors, the complete link, the physical RAM budget, absence
of the discontinued cloud protocol strings, and separation from test fixtures.
It keeps the original 32 KiB stack and 32 KiB heap reservations. Application and
SDK link-time optimization removes unused code; the legacy profile retains its
original compilation and exhaustive symbol checks.
The pinned standalone SDK patch omits Energia's unused eight-client TCP wrapper
registry; the native API listener uses SimpleLink sockets directly. Wi-Fi
connection management and the legacy profile retain their normal behavior.

The optional browser suite exercises the actual portable C API/store behind a
local HTTP harness, with hardware boundaries simulated:

```sh
python -m pip install -r requirements-web-test.txt
python -m playwright install chromium
make web-test
```

`.github/workflows/standalone.yml` runs portable tests, browser workflows and
the ARM firmware build on the feature branch. A CI workflow's presence is not
a claim that its run passed; inspect the run for the specific commit.

## Source recovery background

The base is an editable C/C++ reconstruction of the Pico Pro 0.1.34 application,
with native Energia/TI dependencies and a complete firmware build. It includes
startup and the main menu, brewing and maintenance programs, the display and
encoder, stepper/RFID protocols, networking, storage, and legacy firmware updates.

Names, types and module organization were reconstructed from binary behavior;
this is not the lost original source tree. The generated firmware is not
byte-identical to the original. Production code does not call the original
binary or depend on its application RAM addresses. The reference binary is
used only for analysis, asset extraction and comparison tests.

The repository root contains the current buildable reconstruction. `legacy/`
preserves the complete earlier decompiler-export snapshot from commit
`07359ac60c458efd59bfedc5e020344108cdf1d1`, including its source, analysis and
checksums. It is separate from the current build. The original `mcuimg2.bin`
and license notices remain available at the root; the comparison tests use
the same original image under `reference/original-0.1.34.bin`.
`CONTENTS.sha256` covers the current reconstruction and its packaged artifacts;
the preserved export retains its own `legacy/SHA256SUMS.txt`.

## Source map

| Location | What to modify |
| --- | --- |
| `src/application.c` | Complete setup/main loop and concrete bindings between all modules |
| `src/recipe.c` | Recipe decoding, per-step phase/action engine, session/restart and completion |
| `src/recipe_programs.c` | Manual brew, sous vide, cold brew and PicoStill orchestration |
| `src/recipe_utilities.c` | Prime/reprime, filter removal, vacuum, rinse, first rinse, drain, valve flushing, racking and descale |
| `src/control.c`, `src/phase.c`, `src/pid.c` | Timers, PID, thermal faults, pump/heater timing and phase lifecycle |
| `src/sensors.c`, `src/actuators.c`, `src/state.c` | Thermistors, outputs/interlocks and owned application state |
| `src/device.c`, `src/rfid.c` | Shared SPI, auxiliary controller/stepper/buzzer and Pico's MFRC522 variant |
| `src/ui/` | OLED/graphics/font, screens, menus, input editing, diagnostics and translations |
| `src/network/` | WiFi, HTTP/API, files/preferences/recovery, Still protocol, update/boot info and string helpers |
| `src/local_api.c`, `src/local_store.c`, `src/local_control.c` | Standalone REST resources, persistent records, command queue and stop latch |
| `src/http_server.c`, `platform/energia_http_server.cpp` | Bounded authenticated HTTP transport and native SimpleLink listener |
| `web/`, `tools/embed_web.py` | Browser source and reproducible embedded gzip assets |
| `platform/` | Actual SDK bindings, appliance entry, custom TCP client and Pico memory layout |
| `include/picobrew/` | Typed application interfaces and configuration |
| `tests/`, `analysis/` | Reproducible checks, original-entry maps, limits and measured results |

The original application's 462 indexed entry points are accounted for in
`analysis/completion-audit.json`. Additional recovered callbacks and custom
library routines are also listed. An entry map establishes scope; it does not
mean every possible input or physical operating condition has been tested.

## Legacy build

The verified build uses GNU Arm Embedded 10.3-2021.10, Cortex-M4 Thumb, AAPCS and
software floating point. The pinned Energia commit and downloaded archives
are recorded with SHA-256 hashes in `dependencies/lock.json`. This is a working
rebuild toolchain; PicoBrew's exact historical compiler has not been established.

On Linux x86-64, with Python 3.12+, make and host GCC available:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-test.txt
make bootstrap
make firmware
make verify
```

The bootstrap installs project-local dependencies. It does not install system
packages. Allow roughly 1 GB for the compiler, SDK and build products. Other
hosts can supply a compatible installed ARM toolchain:

```sh
export PICOBREW_ARM_PREFIX=/absolute/path/to/bin/arm-none-eabi-
export PICOBREW_SDK=/absolute/path/to/energia
make firmware
```

The compiler prefix includes the trailing dash. `make build` builds the
application/adapter archives and function-test ELF. `make firmware` adds the
native SDK startup and complete appliance entry using the Pico linker script.
Neither command writes to a device or deploys to a server.

### Firmware outputs

| File under `build/firmware/` | Purpose |
| --- | --- |
| `pico-pro-recovered.bin` | Raw complete application image beginning at 0x20004000 |
| `pico-pro-recovered.elf` | Matching image with symbols/debug information |
| `pico-pro-recovered.map` | Linker map, symbols and dependency providers |
| `pico-pro-recovered.update.txt` | Original getFirmware API response body, including leading/trailing markers |
| `firmware-report.json` | Source/binary hashes, vectors, sections and RAM-budget checks |

The downloadable package also includes these generated artifacts under
`firmware/`, with native compile/link evidence under `firmware/build-evidence/`.
Rebuild into `build/firmware/` after any source change. The separate
`build/application-validation.elf` includes test fixtures and is never an
installation image.

The linker reserves the first 16 KiB of the 256 KiB device RAM for the bootloader,
keeps the native 32 KiB stack and a 32 KiB heap, and checks the resulting image fits.
The native stack is explicitly aligned to eight bytes. Compiler-provided
newlib-nano supplies compact stdio, while the same pinned compiler's full
newlib supplies the allocation family. A bounded adapter preserves the SDK's
heap ownership and supplies the allocator's failure convention. Libm/libgcc
supply math and soft-float operations. No decompiled libc, math library or
guessed SDK object storage is used. See `docs/sdk-build.md` for the exact
boundary, allocation-failure evidence and remaining limits.

## Pointing the legacy application at a community server

Edit `include/picobrew/config.h` before rebuilding. `PICO_SERVER_HOST` defaults
to the original `picobrew.com`; `PICO_SERVER_PORT` defaults to 80. This preserves
the original protocol configuration and does not establish that the original
service is operating. A compatible replacement must implement the API requests
and response formats documented in `docs/network-recovery.md`.
These settings apply to the legacy build. The standalone build ignores them
and has no cloud-server fallback.

The update text is a response body, not a raw flash image or an HTTP response
with headers. It uses Pico's original length/checksum/hex encoding. The
checksum is a legacy compatibility mechanism, not an update signature. The
package test sends the complete generated body through the rebuilt ARM
receiver and checks that its file model receives the exact compiled binary.
It does not activate a boot slot or write a physical machine.

## Legacy verification and evidence

The original firmware and reconstructed application execute in separate ARM
emulators. Test boundaries provide explicit clocks, user input and peripheral
responses. The source image cannot call the reference firmware. Full native
TCP/filesystem/hash bridge tests use the actual production ELF; the other
suites use a separately named test ELF.

The recovered legacy profile passes **29,145 behavioral and integration
scenarios**, plus **20 edit/rebuild checks** and **52 immutable asset checks**.
These counts combine exact original/rebuilt comparisons with separately
identified tests of repaired malformed-input/failure behavior. They are not a
percentage of complete path coverage. `analysis/validation-summary.json`
provides the per-suite totals, exact image hashes and limits.

The production constructor also runs directly in the emulator: four cases
exercise 1,632 assertions, including 238 callback slots across 14 service
tables. Only its clock and destructor-registration boundaries are controlled.
This begins after data/BSS initialization; it does not emulate chip reset or
physical peripheral startup.

All 257 indexed UI application entries now have direct original-versus-rebuilt
ARM cases. The UI ledger records each entry's evidence and separately labels
graphics replacements implemented through composed operations, public fields,
an inlined caller body, or an elided no-op. This establishes entry coverage;
it does not claim exhaustive path or hardware validation.

The checks cover the full 12-bit thermistor ADC domain, PID/controller and
phase transitions, auxiliary-controller exchanges, actual RFID selection,
recipe phases/statuses/actions and utility workflows, original framebuffers,
encoder/menu flows, API and stored-file formats, update streaming, and native
SDK connection behavior. The entry audit requires every indexed application
entry to have a concrete source mapping. The final summary rejects stale
source/test/image hashes, undefined symbols and old-image dependencies.

`make edit-check` changes a sensor correction only inside an isolated temporary
copy, rebuilds ARM code, and checks both changed outputs and unchanged controls.
The packaged production calibration remains at the recovered value.

## Preserved behavior and deliberate repairs

Observed quirks remain in the recovered algorithms unless a repair is recorded.
Examples include Pico-specific RFID collision behavior, the service-button
subtraction direction, certain timeout comparisons, stepper mapping, the URL
backslash escape, and a duplicate recipe-carousel increment.

Defined repairs address concrete original buffer/initialization and I/O failure
problems: bounded recipe/restart/remote strings, terminated WiFi security text,
incomplete Still telemetry rejection, UID buffer capacity, exact file-write
results, and failed boot-info operations preventing successful update/reset.
Those failure cases are tested as changed behavior, not described as equivalent.
The domain documents separate repairs from original-compatible paths.

The rebuild runtime also includes a verified allocation repair. Native tests
found that heap exhaustion could make the initially selected nano allocator
access address zero, while unchecked signed heap increments could escape the
reserved heap. The selected full-newlib allocation family and bounded SDK
adapter pass 47 native allocation scenarios, including exhaustion, overflow,
free/reuse and preservation of the old allocation when reallocation fails.

## What still needs the machine

Software compilation, source reconstruction, image packaging and offline
validation are complete for this revision. Physical acceptance remains open:
first boot and recovery, the actual board/peripheral versions, sensor readings,
outputs and interlocks, rotary valve/RFID operation, networking/filesystem
behavior, and controlled thermal operation. `docs/hardware-validation.md`
records that work explicitly rather than marking unperformed tests as passed.

This project does not bundle a tested flashing procedure or claim a successful
brew. The reference image and experimental replacement are kept separate.

## Further reading

- `docs/recipe-recovery.md`: recipe/session/program semantics, branch coverage and parser repairs.
- `docs/ui-recovery.md`: graphics/assets, menus/input, workflows and UI test limits.
- `docs/network-recovery.md`: server/API, files, Still, update and failure behavior.
- `docs/device-rfid-recovery.md`: SPI handshakes, card selection, device workflows and evidence.
- `docs/state-pid-recovery.md`, `docs/control-recovery.md`, `docs/phase-recovery.md`: control core details.
- `docs/sdk-build.md`, `docs/sdk-boundary-review.md`: dependency provenance and native integration.
- `reference/provenance.json`: identity and public provenance of the 162,792-byte reference image.

Upstream dependencies retain their own licenses and notices. This recovery
does not assign a new license to PicoBrew's original binary or embedded assets.
