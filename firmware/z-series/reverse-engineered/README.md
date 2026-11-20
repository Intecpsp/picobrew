# PicoBrew Z-series OEM reconstruction

The preserved 0.0.116 and 0.0.119 files are
**ESP32 application images containing Xtensa code**, not the S/Pro CC3200
application or its ASCII update envelope. Both images' segment checksums and
appended SHA-256 digests verify. These checks detect corruption; they are not
publisher signatures or evidence that an image fits a particular physical Z.

This directory provides reproducible image mapping, original source-name
evidence, independently host-buildable modules and the assembled Z119 OEM
application. The owned source includes 42 typed enum tables (713 original numeric labels),
thermistor calculations and acquisition, heater/PID control and safety monitors,
the MSP controller protocol, program execution, quantity conversions, EEPROM
storage, program repositories, native grayscale rendering, and Core startup,
event flow and command dispatch.
The concrete ESP32 application now owns and binds Core, Head, every normal
startup/tick service, network client and physical peripheral adapter. It links
against the pinned historical SDK with a real `app_main` entry point.
The recovered diagnostic bodies and their original-execution comparisons are
described in [application assembly](analysis/application-assembly.md) and the
[application console](analysis/application-console.md). Historical
assertion-stopped fixtures and deliberately guarded inputs retain their
documented limits; an assembled application does not make every original
path or hardware behavior verified.

The implemented [standalone profile](../modified/README.md) composes these
recovered controls with the authenticated local API, persistent recipes and
history, device browser, front-panel integration and optional Hub connection.
Its source, reference image and package tools remain in `../modified/`.
Neither profile establishes physical Z1/Z2 mapping, an OEM device partition
layout or qualified installation/OTA support.

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

GitHub CI runs the native comparisons and a separate historical ESP32 application
build in `.github/workflows/zseries-reconstruction.yml`. The latter acquires
hash-pinned compiler/SDK sources using `tools/acquire_esp32.py`. Shared ELF
analysis controls use the pin in `requirements-test.txt`. The target outputs
include the object archives, complete application ELF, map and separate link
evidence. They do not choose a device partition layout or create an installer.
The full recovered module/adapter inventory and its source, header and object
identities are recorded in
[`analysis/integration-target-build.json`](analysis/integration-target-build.json).
CI also checks eight encoder/GPIO interrupt-path symbols at object level and
requires the actual application, heater, encoder, SDK clock/GPIO/lock and
compiler arithmetic symbols in the linked image. See
[`analysis/application-link.json`](analysis/application-link.json) and
[the concrete application](analysis/application-platform.md).

After a fresh complete object build, `python3 tools/refresh_target_reports.py
<object-directory> --dry-run` verifies the source, object, header and generated
inputs and previews the component target-report refresh. Omit `--dry-run` to
record that verified compilation. This updates target evidence only; original
execution, native comparisons and sanitizer runs require their own validators.

The default build requires a C++11 compiler. The ordinary tests use Python's
standard library and a native compiler; recipe tests also use the selected
ArduinoJson dependency, and the independent Azure HMAC comparison needs the
OpenSSL development package (`libssl-dev` on Ubuntu). Ghidra is required only to reproduce
the original-instruction execution evidence, not to build the owned source
or run its portable tests.

## Reconstructed source and validation

| Module | Evidence | Validation |
| --- | --- | --- |
| `include/zseries/enums.hpp`, `src/enums.cpp` | Original pointer tables, display strings, and observed unsigned guards in `analysis/enums.json` | All 713 emitted names reread from preserved bytes; native enum adapters checked at valid and invalid indices |
| `include/zseries/temperature.hpp`, `src/temperature.cpp` | `MAX11613.h` code and DROM calibration records; [address map](analysis/temperature.md) | All coefficient bits checked; 16,384 native temperature results compared with original Xtensa instruction execution at explicit SDK boundaries |
| `include/zseries/hex_detection.hpp`, `src/hex_detection.cpp` | Original `0x400dd9f4` decision and exact sensor conversion arguments; [behavior](analysis/hex-detection.md) | 8,439 original-instruction cases match native source, covering all nine result codes; 119 portable goldens |
| `include/zseries/runtime_instruction.hpp`, `src/runtime_instruction.cpp` | Original 8-byte instructions, 58 opcodes, argument checks and fixed point decoder; [address map](analysis/runtime-instruction.md) | 29,632 original argument checks and 1,852 original decoder cases match native source; 1,792 portable goldens |
| `include/zseries/msp_protocol.h`, `src/msp_protocol.c` | Original auxiliary-controller framing, state, setup and update sequence; [scope](analysis/msp-protocol.md) and [bound diagnostic continuation](analysis/msp-continuation.md) | The full-return suite covers 123 production/recovery/self-test cases and 320 returning assertions. The older 5,553-case fixture retains its 1,812 assertion-stopped prefixes and describes the separate legacy guarded interface |
| `include/zseries/heater.hpp`, `src/heater.cpp` | Original PID, heater GPIO and 1 s/20 ms timer callbacks; [address map](analysis/heater.md) | 583 scenarios / 930 actions match original instructions; 24,180 double fields bit-identical and exact GPIO events |
| `include/zseries/sensor_acquisition.hpp`, `src/sensor_acquisition.cpp` | Original I²C setup/polling, ten-sample smoothing and Core temperature publication; [scope](analysis/sensor-acquisition.md) | 340 transaction/smoother scenarios plus 255 complete ADC/Core continuation cases, including 597 returning assertions; explicit bus and clock boundaries |
| `include/zseries/auxiliary_sensors.hpp`, `src/auxiliary_sensors.cpp` | Original LIS3DHTR/BMP280 bus operations, smoothing and compensation; [scope and preserved OEM defect](analysis/auxiliary-sensors.md) | 641 complete original scenarios, 32 real assertion continuations, 3,205 bit-identical double fields and exact bus/calibration/integer state |
| `include/zseries/safeties.hpp`, `src/safeties.cpp`, `core_control_state.*` | Original timed interlocks, filter input and runtime flag integration; [scope](analysis/safeties.md) | 660 scenarios / 1,435 original ticks match; 10,045 double fields bit-identical and exact latch/deadline/GPIO state |
| `include/zseries/program_runtime*.hpp`, `src/program_runtime*.cpp`, `quantity.*` | Original program dispatcher, waiters, transitions, manual steps and runtime ticks; [scope](analysis/program-runtime.md) and [continuation report](analysis/runtime-continuation-validation.json) | All 5,792 cases complete in the newer replay, including the 1,023 previously stopped cases and 1,142 returning assertions; exact state, expansion stack and ordered effects compared |
| `include/zseries/eeprom.hpp`, `src/eeprom.cpp` | Original configuration schema and EEPROM lifecycle; [scope](analysis/eeprom.md) and [continuation evidence](analysis/eeprom-continuation.md) | All 2,363 original cases complete, including 391 returning assertions; real SDK EEPROM adapter and diagnostic commands are bound |
| `include/zseries/program_repository.hpp`, catalog/repository/writer source | Original built-in catalog, buffered program storage and coffee generation; [scope](analysis/program-repository.md) | 669 observations across 34 bodies: 538 completed paths and 131 assertion-stopped prefixes; all 23 built-in programs load through the real recovered Runtime |
| `program_recipe.hpp`, recipe builders and ArduinoJson adapter | Local JSON, Zymatic/Z-recipe and PicoPak transformations; [scope and parser compatibility](analysis/program-recipes.md) | Historical 592-case fixture: 497 complete application matches, 13 first-assertion observations and 82 parser cases; separate 32-bit replay. Fresh continuation evidence documents the 13 bounded-policy deviations |
| `include/zseries/core_startup.hpp`, `src/core_startup.cpp`, `platform/esp32/` | Original Core startup ordering, constructors, peripheral assignments and SDK calls; [startup evidence](analysis/core-startup.md) and [concrete owner](analysis/application-platform.md) | Both boiler startup traces match 92 observed events; composed application and required SDK/interrupt symbols are checked by the full link |
| `system_tick.hpp`, `system_tick.cpp`, ESP32 watchdog adapter | Original watchdog reset, loop timing and maximum-duration warning; [scope and assertion boundaries](analysis/system-tick.md) | 228 original scenarios match exact integer/double state and ordered calls; real historical watchdog/clock adapter compiles |
| `include/zseries/core_flow.hpp`, `src/core_flow.cpp` | Core state transitions, component order, event priority and session gates; [scope](analysis/core-flow.md) | 1,672 original scenarios / 17 bodies match 17,442 ordered service calls, state and credentials |
| `include/zseries/core_commands.hpp`, `src/core_commands.cpp` | State-dependent dispatcher, program-start metadata, boiler and reset operations; [scope](analysis/core-commands.md) | 1,662 original scenarios across 40 bodies; 1,106 ordered effects and exact session/Runtime/PID state |
| `wifi.hpp`, `wifi.cpp`, ESP32 Wi-Fi adapter | Original connection/scan/AP state machine, credentials, DNS gate and retry timer; [scope](analysis/wifi.md) | 1,240 original scenarios across 17 bodies; real Core mailbox integration and historical SDK target compilation |
| `runtime_events.hpp`, `runtime_events.cpp` | Original four-field Runtime mailbox, Core forwarding and Head notification identity; [scope](analysis/runtime-events.md) | 288 independent original copy/drop/read cases; composed Core-to-Head notification check; full 1,672 Core-flow and 1,662 command scenarios replayed |
| `program_list.hpp`, `program_list.cpp` | Original Core event payload, recipe references and readiness publication; [scope](analysis/core-event.md) | 27 original cases match initialization, reset and occupied-mailbox behavior; recipe-ready events retain the original payload ownership |
| `local_http_server.hpp`, `local_http_server.cpp` | Original incoming connection lifecycle, request buffering and response framing; [scope](analysis/local-http-server.md) | 169 original cases match ordered transport calls, parser state and dispatch; host failure paths and historical target compilation pass |
| `local_http_state.hpp`, `local_http_state.cpp` | Original complete `/state` response and live control-state bindings; [scope](analysis/local-http-state.md) | 80 original cases match the exact JSON bytes; concrete sensor/heater/MSP/Runtime snapshots and output failures are checked |
| `pico_still.hpp`, `pico_still.cpp` | Original Still controller, scan selection, authorization/network handoff and retry/polling; [scope](analysis/pico-still.md) | 2,284 original scenarios across 28 bodies; sanitizer integration with recovered Wi-Fi/Core mailboxes and historical target compilation |
| `still_client.*`, `pico_still_sensors.*` | Original HTTP command/response protocol, form encoding, sensor CSV, sample derivatives and statistics; [scope](analysis/still-client.md) | 4,186 original scenarios across 50 bodies; fragmented-response integration with the recovered controller and historical target compilation |
| `http_client.*`, `still_http.*`, ESP32 Client adapter | Original outgoing URI/request writer, six-state response engine and real StillClient bridge; [scope](analysis/http-client.md) and [continuation report](analysis/http-client-continuation-validation.json) | 972 complete original cases, including request/timing dumps and 594 returning assertions; separate [live diagnostic](analysis/http-client-console.md) compares 55 scenarios through the actual asynchronous client |
| `local_http_application.*`, ESP32 incoming socket adapter | Concrete POST recipe parsing, full Core-event publication and live `/state`; [scope and bounded input-validation policy](analysis/local-http-application.md) | 68 composed original cases across 70 bodies: 62 exact, six intentional rejections of defined OEM continuations; fragmented-request/ownership/write-failure integration and historical target compilation |
| `buzzer.*`, ESP32 LEDC adapter | Original enable/timer, System alarm and five console commands; [scope](analysis/buzzer.md) | 561 original scenarios across eleven bodies (including the returning assertion helper) and a constructor slice; composed Head alarm/SDK checks and historical target compilation |
| `head_flow.hpp`, `head_flow.cpp` | Original Head event priority, alarm timeout, redraw timer and command routing; [scope](analysis/head-flow.md) | 751 original scenarios across six bodies match state, pending slots and ordered service/clock calls |
| `head_events.hpp`, `head_events.cpp` | Runtime alarm/notification decisions and encoder-button handlers; [scope](analysis/head-events.md) | 13,812 original cases match ordered effects, notification ID and hijack state; alarm/presentation calls are typed fixture boundaries and are bound to real services by the composed Head |
| `head_numeric_inputs.hpp`, `head_numeric_inputs.cpp` | Temperature, duration, location, quantity and recipe-strength handlers; [scope](analysis/head-numeric-inputs.md) | All 1,637 cases now complete, including 234 real assertion continuations; command payloads, parameter bits, input flags, counters and delays are compared |
| `head_text_input.hpp`, `head_text_input.cpp` | Password priority, recipe-reference routing and bounded kit-code storage; [scope and C-library boundary](analysis/head-text-input.md) | 922 original cases match retained state and typed command payloads; the composed Head copies commands into owned Core envelopes |
| `head_network_events.hpp`, `head_network_events.cpp` | Wi-Fi presentation gates and Azure group-session choices/coffee normalization; [scope](analysis/head-network-events.md) | 1,444 completed original cases match localized action text, menu commands, parameter bits and continuation after returning assertions |
| `head_encoder.hpp`, `head_encoder.cpp` | Encoder widget order, selections, duration steps and timer resets; [scope](analysis/head-encoder.md) | 3,099 traces: 3,026 complete matches and 73 bounded character-selection prefixes; original continuation is retained separately for the guarded cases |
| `physical_inputs.*`, ESP32 GPIO/interrupt adapter | Physical buttons, encoder sampling/ISR/ring, decoding and event production; [scope](analysis/physical-inputs.md) | 948 original cases across 22 bodies; composed Head/Core input checks and eight ISR-path symbols verified in IRAM against the pinned SDK |
| `ui_screen_tick.hpp`, `ui_screen_tick.cpp` | Normal/combined screen timing and ordered periodic widget dispatch; [scope](analysis/ui-screen-tick.md) | 2,148 original cases match strict/equal deadline guards, waiting frames, 32-bit timer wrap and ordered widget calls |
| `display.hpp`, `display.cpp`, ESP32 display adapter | Original SSD1325 GPIO/SPI, native 4-bit framebuffer and packed geometry; [scope](analysis/display.md) | 1,010 original scenarios across 18 bodies match framebuffer, transfer and bus events |
| Display fonts and text source | Five byte-verified original font tables and native text renderer; [scope](analysis/display-text.md) | 503 original scenarios across 19 text bodies match native source |
| `display_effects.hpp`, `display_effects.cpp` | Original gradients, caret, animation and slides; [scope](analysis/display-effects.md) | 989 original scenarios match complete frame/transfer buffers and serialized GPIO/SPI/delay output |
| `ui_catalog.hpp`, `ui_catalog.cpp` | 18 localized categories, 102 screen bodies and 16 non-font bitmaps; [scope and language guard](analysis/ui-catalog.md) | 2,706 original lookup cases match returned text and unchanged-output behavior; all bitmap descriptors and 3,183 pixel bytes match preserved data |

The complete [Head application](analysis/head-application.md) binds the event
consumers, editors, menus, recurring painters and shared display context.
[Core/platform assembly](analysis/application-platform.md) supplies its live
observations and the actual startup/tick services. [Application assembly](analysis/application-assembly.md)
also indexes the complete hardware, MSP, Runtime/Still, backend-data, Basic,
live HTTP, [live HttpBackend](analysis/http-backend-network.md) and Azure
diagnostic bodies. Network/Int/All orchestration has its own
comparison with explicit child-body boundaries. These composed checks supplement
the per-module comparisons above.

The original assertion helper logs, increments a diagnostic counter, waits
200 ms and returns. A fixture stopped at that call proves only the preceding
execution prefix. [Assertion evidence coverage](analysis/assertion-coverage.md)
classifies the older fixtures; newer module reports explicitly distinguish
complete matches from deliberate bounded-policy deviations.

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

## Preserved image evidence

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

These strings identify original components; a filename does not
prove an electrical connection, bus address, pin assignment, struct layout or
full algorithm. The recovered ESP32 **MSP controller protocol** has its own
source and instruction evidence; it is not replaced by S/Pro actuator logic.
Firmware build labels cannot replace independent physical-model identification.

## Reconstruction and installation boundaries

The recovered application and separate standalone API/browser/Hub profile are
implemented as readable, editable source with traceable evidence.
The lost author's original comments, local changes and source organization
cannot be recovered verbatim from the application image. Decompiled output
must be checked for function boundaries, calling conventions, object layouts,
callback dispatch and peripheral effects before it becomes production source.

Machine-specific actuator mapping, sensor acquisition, thermal interlocks,
controller boot/update behavior and persistent-storage/boot behavior still
require physical qualification on the intended machine. Software comparisons
and reference packages do not establish physical brewing, flashing, OTA recovery
or a byte-identical rebuild of the vendor application. The [family parity
matrix](../../../docs/firmware-parity.md) separates implemented standalone
features from those installation limits and the missing Zymatic image.

The initial Ghidra inventory of 5,274 functions was incomplete. Address-taken
entry discovery adds 436 definitions, producing an expanded inventory of 5,710
functions (5,709 decompiled). The actual static initialization table contains
45 entries; constructor and callback evidence is retained in
[`analysis/startup-bindings.json`](analysis/startup-bindings.json). Additional
unreferenced entry candidates remain unvalidated. The concrete application binds
the recovered startup services and completes the historical SDK link. Diagnostic
suite evidence, deliberate guarded inputs and installation boundaries are
tracked in [application assembly](analysis/application-assembly.md).
