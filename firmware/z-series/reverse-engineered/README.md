# PicoBrew Z-series OEM reconstruction

Reconstruction is in progress. The preserved 0.0.116 and 0.0.119 files are
**ESP32 application images containing Xtensa code**, not the S/Pro CC3200
application or its ASCII update envelope. Both images' segment checksums and
appended SHA-256 digests verify. These checks detect corruption; they are not
publisher signatures or evidence that an image fits a particular physical Z.

This directory provides reproducible image mapping, original source-name
evidence, and independently host-buildable reconstructed modules. The owned
source currently includes 42 typed enum tables (713 original numeric labels),
thermistor calculations and acquisition, heater/PID control and safety monitors,
the MSP controller protocol, program execution, quantity conversions, EEPROM
storage, program repositories, native grayscale rendering, and Core startup,
event flow and command dispatch.
An ESP32 adapter binds recovered operations to the historical SDK. It
does not yet provide a complete linked OEM application, a custom firmware
image, or installation support. The Z1/Z2 mapping remains unestablished.

## Preserved inputs and reproducible analysis

The original files remain unchanged in [`../oem/upstream/`](../oem/upstream/).
[`analysis/images.json`](analysis/images.json) records their exact SHA-256,
entry point, all 12 load segments, segment hashes, and footer verification.
[`analysis/source-inventory.json`](analysis/source-inventory.json) records
original sketch-header names, enum type names, SDK source paths and build
literals with both virtual addresses and file offsets.

```sh
make           # Build the reconstructed host library; not a flash image
make analyze   # Regenerate image reports and typed enum source from evidence
make verify    # Build, test original-byte/behavior evidence, check report freshness
```

The concrete recipe JSON adapter uses the hash-pinned dependency described in
`dependencies/arduinojson.json`. To build its separate native archive:

```sh
python3 tools/acquire_arduinojson.py
make json-adapter
# Or use an existing verified checkout:
make json-adapter ARDUINOJSON_DIR=<pinned-checkout>
```

GitHub CI runs the native comparisons and a separate historical ESP32 object
build in `.github/workflows/zseries-reconstruction.yml`. The latter acquires
hash-pinned compiler/SDK sources using `tools/acquire_esp32.py`. Shared ELF
analysis controls use the pin in `requirements-test.txt`. The target output is
a static archive and compilation evidence, not an installable firmware image.

The default build requires a C++11 compiler. The ordinary tests use Python's
standard library and a native compiler; recipe tests also use the selected
ArduinoJson dependency. Ghidra is required only to reproduce
the original-instruction execution evidence, not to build the owned source
or run its portable tests.

## Reconstructed source and validation

| Module | Evidence | Validation |
| --- | --- | --- |
| `include/zseries/enums.hpp`, `src/enums.cpp` | Original pointer tables, display strings, and observed unsigned guards in `analysis/enums.json` | All 713 emitted names reread from preserved bytes; native enum adapters checked at valid and invalid indices |
| `include/zseries/temperature.hpp`, `src/temperature.cpp` | `MAX11613.h` code and DROM calibration records; [address map](analysis/temperature.md) | All coefficient bits checked; 16,384 native temperature results compared with original Xtensa instruction execution at explicit SDK boundaries |
| `include/zseries/hex_detection.hpp`, `src/hex_detection.cpp` | Original `0x400dd9f4` decision and exact sensor conversion arguments; [behavior](analysis/hex-detection.md) | 8,439 original-instruction cases match native source, covering all nine result codes; 119 portable goldens |
| `include/zseries/runtime_instruction.hpp`, `src/runtime_instruction.cpp` | Original 8-byte instructions, 58 opcodes, argument checks and fixed point decoder; [address map](analysis/runtime-instruction.md) | 29,632 original argument checks and 1,852 original decoder cases match native source; 1,792 portable goldens |
| `include/zseries/msp_protocol.h`, `src/msp_protocol.c` | Original auxiliary-controller framing, state, setup and update sequence; [scope](analysis/msp-protocol.md) | 5,553 original cases across 24 bodies; explicit I²C, delay, logging and assertion boundaries |
| `include/zseries/heater.hpp`, `src/heater.cpp` | Original PID, heater GPIO and 1 s/20 ms timer callbacks; [address map](analysis/heater.md) | 583 scenarios / 930 actions match original instructions; 24,180 double fields bit-identical and exact GPIO events |
| `include/zseries/sensor_acquisition.hpp`, `src/sensor_acquisition.cpp` | Original I²C setup/polling, ten-sample smoothing and Core temperature publication; [scope](analysis/sensor-acquisition.md) | 340 transaction/smoother scenarios, 3,116 updates, 37,392 bit-identical double fields; explicit bus and clock boundaries |
| `include/zseries/auxiliary_sensors.hpp`, `src/auxiliary_sensors.cpp` | Original LIS3DHTR/BMP280 bus operations, smoothing and compensation; [scope and preserved OEM defect](analysis/auxiliary-sensors.md) | 617 original scenarios, 3,085 bit-identical double fields, exact bus/calibration/integer state |
| `include/zseries/safeties.hpp`, `src/safeties.cpp`, `core_control_state.*` | Original timed interlocks, filter input and runtime flag integration; [scope](analysis/safeties.md) | 660 scenarios / 1,435 original ticks match; 10,045 double fields bit-identical and exact latch/deadline/GPIO state |
| `include/zseries/program_runtime*.hpp`, `src/program_runtime*.cpp`, `quantity.*` | Original program dispatcher, waiters, transitions, manual steps and runtime ticks; [scope](analysis/program-runtime.md) | 5,792 original cases across 52 original bodies; typed heater/MSP integration checked |
| `include/zseries/eeprom.hpp`, `src/eeprom.cpp` | Original configuration schema and EEPROM lifecycle; [scope](analysis/eeprom.md) | 2,363 original cases across 19 bodies; real SDK EEPROM adapter compiles |
| `include/zseries/program_repository.hpp`, catalog/repository/writer source | Original built-in catalog, buffered program storage and coffee generation; [scope](analysis/program-repository.md) | 669 original cases across 34 bodies; all 23 built-in programs load through the real recovered Runtime |
| `program_recipe.hpp`, recipe builders and ArduinoJson adapter | Local JSON, Zymatic/Z-recipe and PicoPak transformations; [scope and parser compatibility](analysis/program-recipes.md) | 592 original cases / 70 bodies; all 510 application outputs exact, separate 32-bit replay and explicit malformed-token exceptions |
| `include/zseries/core_startup.hpp`, `src/core_startup.cpp`, `platform/esp32/` | Original Core startup ordering, constructors, peripheral assignments and SDK calls; [remaining application boundary](analysis/core-startup.md) | Both boiler startup traces match 92 observed events; target objects compile against pinned GCC 5.2 / Arduino ESP32 1.0.1 |
| `include/zseries/core_flow.hpp`, `src/core_flow.cpp` | Core state transitions, component order, event priority and session gates; [scope](analysis/core-flow.md) | 1,672 original scenarios / 17 bodies match 17,442 ordered service calls, state and credentials |
| `include/zseries/core_commands.hpp`, `src/core_commands.cpp` | State-dependent dispatcher, program-start metadata, boiler and reset operations; [scope](analysis/core-commands.md) | 1,662 original scenarios across 40 bodies; 1,106 ordered effects and exact session/Runtime/PID state |
| `wifi.hpp`, `wifi.cpp`, ESP32 Wi-Fi adapter | Original connection/scan/AP state machine, credentials, DNS gate and retry timer; [scope](analysis/wifi.md) | 1,240 original scenarios across 17 bodies; real Core mailbox integration and historical SDK target compilation |
| `runtime_events.hpp`, `runtime_events.cpp` | Original four-field Runtime mailbox, Core forwarding and Head notification identity; [scope](analysis/runtime-events.md) | 288 independent original copy/drop/read cases; composed Core-to-Head notification check; full 1,672 Core-flow and 1,662 command scenarios replayed |
| `head_flow.hpp`, `head_flow.cpp` | Original Head event priority, alarm timeout, redraw timer and command routing; [scope](analysis/head-flow.md) | 751 original scenarios across six bodies match state, pending slots and ordered service/clock calls |
| `head_events.hpp`, `head_events.cpp` | Runtime alarm/notification decisions and encoder-button handlers; [scope](analysis/head-events.md) | 13,812 original cases match ordered effects, notification ID and hijack state; actual alarm timers and screen presentation remain service boundaries |
| `head_numeric_inputs.hpp`, `head_numeric_inputs.cpp` | Temperature, duration, location, quantity and recipe-strength handlers; [scope and assertion repair](analysis/head-numeric-inputs.md) | 1,637 original cases match command payloads, parameter bits, input flags and ordered screen effects |
| `display.hpp`, `display.cpp`, ESP32 display adapter | Original SSD1325 GPIO/SPI, native 4-bit framebuffer and packed geometry; [scope](analysis/display.md) | 1,010 original scenarios across 18 bodies match framebuffer, transfer and bus events |
| Display fonts and text source | Five byte-verified original font tables and native text renderer; [scope](analysis/display-text.md) | 503 original scenarios across 19 text bodies match native source |
| `display_effects.hpp`, `display_effects.cpp` | Original gradients, caret, animation and slides; [scope](analysis/display-effects.md) | 989 original scenarios match complete frame/transfer buffers and serialized GPIO/SPI/delay output |
| `ui_catalog.hpp`, `ui_catalog.cpp` | 18 localized categories, 102 screen bodies and 16 non-font bitmaps; [scope and language guard](analysis/ui-catalog.md) | 2,706 original lookup cases match returned text and unchanged-output behavior; all bitmap descriptors and 3,183 pixel bytes match preserved data |

The temperature comparison was exact in the recorded native environment;
portable tests retain representative original-execution goldens and a small
cross-libm tolerance. This validates the recovered calculation and constants,
not the original full CPU, physical sensors, or a booted machine. Diagnostic
formatting, compiler arithmetic, and `log` are explicit execution boundaries.
The library uses owned constants and source, with no OEM image dependency at
runtime. Full scope and reproducible commands are in
[`analysis/temperature.md`](analysis/temperature.md).

The reader follows Espressif's
[ESP32 image format](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/firmware-image-format.html).
It handles the legacy 24-byte header directly and does not assume that these
older images have the modern ESP-IDF application-description structure.
No command in this analysis workflow flashes or contacts a machine.

## Evidence established so far

Both images enter at `0x40083918` and contain mapped flash code/data plus
internal-RAM segments. The flash configuration byte is a build setting; it
does not identify installed flash or a machine variant. The files are
applications, not complete flash backups: their contents do not establish the
bootloader, partition table, persisted configuration, or permitted update slots
on a physical machine.

The original application identifies itself as **ZSeries**. The 0.0.119 build
strings include commit `b6b00f995af567cc3010d27a1002c3789826e245`, `master`,
`Apr 17 2020`, and `08:34:08`. The 0.0.116 build includes commit
`109c62acc262a95094ef083f12bcf071bb430299`, `Mar 23 2020`, and `16:38:23`.
Both contain ESP-IDF identification `v3.3-beta1-179-ge931fe9f5-dirty`,
Arduino ESP32 source paths, and FreeRTOS version string `V8.2.0`.
The same `dirty` SDK identification is present in Espressif's Arduino ESP32
1.0.1 `libesp32.a`; the suffix alone does not establish a PicoBrew modification.
That official SDK is a concrete library-matching lead, not proof that the
entire vendor core or all build flags match. Its compiler identification is
GCC 5.2.0 / crosstool-ng 1.22.0-80-g6c4433a. The original complete build
configuration has not been established.

The source inventory exposes 65 original sketch headers in 0.0.116 and 67 in
0.0.119. The latter adds `HttpServer.h` and `ProgramParser.h`. Specific evidence
in 0.0.119 includes:

| Area | Original evidence | File offset |
| --- | --- | --- |
| Separate controller | `Msp.h`, boot/app switching, command/response diagnostics | `0x465c` |
| Safety logic | `Safeties.h`, sensor-temperature/delta and filter checks | `0x1944` |
| ADC | `MAX11613.h` | `0x19db` |
| Heater controller | `Heater.h`, PID diagnostics | `0x1b81` |
| Display | `Name = SSD1325` diagnostic; screen width/height reported separately | `0x1c02` |
| Pressure-related sensor | `BMP280.h` | `0x2e83` |
| Accelerometer | `LIS3DHTR.h` | `0x5261` |
| Program execution | `Runtime.h`, `Program.h`, `RuntimeState.h` | `0x1f01`, `0x1f9f`, `0x1d6c` |
| Local HTTP implementation | `HttpServer.h` | `0xfb4` |

These strings identify code that must be reconstructed; a filename does not
prove an electrical connection, bus address, pin assignment, struct layout or
full algorithm. In particular, the ESP32 application's **MSP controller
protocol** must be recovered before adapting S/Pro controls. Firmware build
labels cannot replace independent physical-model identification.

## Reconstruction boundary

The intended result is readable, editable reconstructed source with traceable
binary evidence, followed by the standalone API/browser/Hub functionality.
The lost author's original comments, local changes and source organization
cannot be recovered verbatim from the application image. Decompiled output
must be checked for function boundaries, calling conventions, object layouts,
callback dispatch and peripheral effects before it becomes production source.

Machine-specific actuator mapping, sensor acquisition, thermal interlocks,
controller boot/update protocol, persistent-storage format and boot behavior
remain qualification gates. There is no claim of physical brewing, flashing,
or runtime feature parity at this stage.

The initial Ghidra inventory of 5,274 functions was incomplete. Address-taken
entry discovery adds 436 definitions, producing an expanded inventory of 5,710
functions (5,709 decompiled). The actual static initialization table contains
45 entries; constructor and callback evidence is retained in
[`analysis/startup-bindings.json`](analysis/startup-bindings.json). Additional
unreferenced entry candidates remain unvalidated. The complete linked
application still requires the complete Head/UI, composed Core services, network
backends, PicoStill and remaining platform/service objects; the startup interface
requires these implementations and does not substitute successful no-ops.
