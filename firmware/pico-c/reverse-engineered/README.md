# Pico C: OEM reconstruction

This directory contains readable, typed C application source and C++ bindings for
the Pico C CC3200 runtime. Both original display variants build into complete
application images using the pinned Energia/TI SDK, compiler libraries, native
startup, and an explicit linker layout. Test fixtures are excluded from that link.

This is a reconstruction of behavior, not recovered vendor source text or a
byte-identical rebuild. Names, types, module boundaries, and comments have been
reconstructed. The compiler and SDK are identified in
[`dependencies/lock.json`](dependencies/lock.json); their successful integration
does not prove that every library implementation matches the OEM binary.

## Original images and display variants

The unmodified update envelopes remain in [`../oem/upstream/`](../oem/upstream/).
`tools/inspect_oem.py` verifies their envelope hashes, original transport checksums,
decoded image hashes, and Cortex-M vectors before analysis or execution.

| Build selection | OEM file | Controller | Logical graphics size | OEM rotation |
| --- | --- | --- | --- | --- |
| `ssd1325` | `pico_c_0_1_34.bin` | SSD1325 | 128 × 64 | 2 |
| `ssd1309` | `pico_c_0_1_34_alt.bin` | SSD1309 | 128 × 32 | 0 |

Both images identify themselves as Pico C v0.1.34. They use the C custom font,
compact two-row screens, and scrolling instructions. The display selection is
compiled into the application. These filenames and software identifiers do not
establish which physical machine has which controller or make the images
interchangeable.

## Source organization

- `src/` contains application composition, sensors, PID and timer control,
  actuators and interlocks, encoder input, RFID, recipe parsing and execution,
  cleaning/service procedures, storage, network protocols, and the C display/UI.
- `include/pico_c/` declares explicit component and platform interfaces.
- `platform/` binds those interfaces to actual Energia GPIO, ADC, SPI, timers,
  Wi-Fi, serial flash, HTTP transport, MCU reset, and native startup. The complete
  image does not substitute successful test callbacks for hardware services.
- `tests/` runs independently compiled source against instructions from each
  original image. Controlled boundaries record input, clock, GPIO, display,
  network, storage, and mechanical-service effects.
- `analysis/` records original-image provenance, entry correspondence, extracted
  assets, test results, and the limits of each comparison. Structural matches
  are review evidence and are not treated as behavioral tests.

The preserved S/Pro reconstruction at
`ea887709e72c493456583114034cde4aad746099` supplied readable candidates and shared
test machinery. C-specific constants, call order, layouts, recipe behavior,
assets, and display protocols were then recovered from the two C images.
`analysis/source-reuse.json`, `analysis/ui-source-reuse.json`, and
`analysis/platform-source-reuse.json` distinguish that reuse from independent
original-image evidence. No archived S/Pro file is changed by this component.

## Build and verify

Run from this directory with Python 3.12+ and a native C compiler. The pinned
compiler bootstrap supports Linux x86-64.

```sh
python3 -m pip install -r requirements-test.txt
make bootstrap
make firmware DISPLAY=ssd1325
make firmware DISPLAY=ssd1309
make verify
```

Existing dependencies can be selected with `PICOBREW_ARM_PREFIX` (the complete
`arm-none-eabi-` tool prefix) and `PICOBREW_SDK` (the Energia source directory).
`PYTHON` selects the Python interpreter. `make host-test` runs the native/original
comparisons; `make target-test DISPLAY=ssd1309` also builds and checks the selected
ARM variant.

Each complete build is isolated under `build/firmware/<display>/` and contains an
ELF, raw binary, linker map, legacy update envelope, source hashes, and an image
layout report. The builder checks vectors, entry points, loadable RAM bounds,
heap reservation, undefined symbols, and absence of test implementations. An
archive from a different display or changed source is rejected.

`build_core.py` is a separate function-test build. Its
`application-validation.elf` contains explicit test fixtures and must never be
installed on a machine. The complete build uses `platform/firmware_main.cpp`.

## Verification and explicit repairs

The suites cover exhaustive ADC inputs, actuator/filter behavior, control/phase
state, RFID, recipe execution and persistence, network/storage protocols,
application startup/menu orchestration, both graphics/transfer implementations,
localized screen composition, scrolling, menus, editor input, and diagnostics.
Some suites compare complete framebuffer bytes and SPI/GPIO traces; others
compare higher-level effects at their documented service boundaries. The latter
do not independently validate the substituted service.

The reconstruction also makes bounded-memory repairs where the original reads
or writes outside an object. These are deliberately distinguished from OEM
equivalence:

- The SSD1309 image renders a 512-byte logical framebuffer but its flush driver
  reads eight pages (1,024 bytes). The rebuilt display reserves a full transfer
  window and initializes its extra half. Original adjacent RAM contents cannot
  be reproduced by assuming a different object layout.
- The French clear-Wi-Fi prompt overflows the original stack buffer. Tests
  observe its corrupt return address after the text is emitted; the rebuilt
  source preserves the text in a sufficiently large buffer and returns normally.
- Scrolling text, remotely supplied strings, recipe bitmaps, and menu indices
  have explicit bounds. Unsupported still setup indices return without reading
  an uninitialized stack string. The original C bitmap decoder's extra write
  beyond 512 bytes is rejected.
- Invalid action/title indices are rejected rather than reading beyond their
  actual tables. Network/recipe repair cases are reported separately from
  ordinary original-versus-rebuilt comparisons.
- The Wi-Fi address screen terminates its 18-character SSID explicitly. The
  original left a byte uninitialized between its copied text and terminator.

OEM quirks that do not require a memory repair remain visible in this baseline:
localized wording, compact row choices, timer and scrolling behavior, a
first-rinse displayed duration that differs from its loop duration, and unusual
rows in the still fan screen. Custom behavior belongs in `../modified/`.

## Qualification boundary

No image in this reconstruction has been physically installed or used to run a
brew as part of these tests. Software builds and instruction comparisons cannot
establish electrical behavior, sensor calibration, interrupt scheduling, pump
or heater timing, controller identity, flash recovery, or safe operation on a
particular appliance. The auxiliary timer's setup and ISR are recovered, but
its original purpose remains unidentified.

This baseline retains the original protocol and application behavior. Standalone
Hub/API feature support is tracked separately in `../modified/` and the
repository model manifest. Building this baseline does not enable a model in
the custom-firmware router.
