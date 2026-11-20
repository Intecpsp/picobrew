# PicoStill OEM reconstruction

This component reconstructs the preserved **PicoStill 0.0.35** ESP32 application.
It is editable source derived from the image, assembled and linked as a complete
application against the pinned historical ESP32 SDK. It is not recovered vendor
source or a byte-identical rebuild. The ELF/link evidence does not establish a
device flash layout, physical installation, or hardware qualification.

## Recovered and checked

`src/core.cpp` implements 16 reviewed OEM instruction entries: both returning
assertion forms, exponential moving average, caller-parameterized thermistor
conversion, sensor sample/read/range checks, fan speed/issue checks, pump resume
predicate, error-to-animation mapping, first-error latching and physical button
transitions/polling. `src/enums.cpp` contains 111 original numeric labels from
eight address-verified tables. `src/hardware.cpp` reconstructs 19 further
entries for GPIO outputs, PWM setup/control, tachometer sampling and smoothing,
fan power coordination, the three-color LED interpreter, and the six-state
vacuum-seal/clog/leak pump test. Its 39 LED programs are recovered byte for byte
by `tools/recover_animations.py`. Runtime code owns its data and uses no OEM image
or absolute application RAM.

`src/sensors.cpp` adds the original four channel calibrations, three/four-channel
12-bit ADC reads, the pressure command/status/conversion sequence, and the
temperature/pressure quality and average updates. Its calibration records are
checked against writes performed by original static initializer `0x400d8688`.
The initializer was found through the original init-array pointer; four reviewed
extra entry points are pinned in `analysis/additional-entry-evidence.json`.

`src/controller.cpp` joins those modules into the nine-state hardware/program
controller. It implements sensor enable/reset/polling, the 200-entry pressure
recording buffer, snapshots and hardware error priority; connection and pin
diagnostic tests; program start/stop, cleaning duration and overtemperature
shutdown/restart; and full-test sequencing with the original combined errors.
The early controller fixture records observers/result serializers as explicit
boundaries; the application report implementation and later integration fixtures
now connect those callbacks to the real sensor CSV and diagnostic serializers.
The implementation owns all runtime state and can use
the concrete ESP32 driver without reading original firmware memory.

`src/application.cpp` integrates the fourteen application states, queued button
and Wi-Fi events, the three-item manual program menu, start/stop requests and
the original ordered poll loop. Controller, sensors, LEDs and button bodies run
inside the application replay. Network, console, persistence, update transport,
HTTP listening and system restart calls are explicit boundaries in that early
fixture. `Machine` now connects them to the implemented services and concrete
ESP32 SDK adapters.

`src/storage.cpp` implements the original NVS-backed credential storage:
512-byte allocation, existing-record growth/truncation, 100-byte credential
load/save and allocation/NVS failure continuations. It uses a concrete `nvs_*`
adapter rather than the development SDK's unrelated raw-partition EEPROM
implementation. The original save wrapper calls `nvs_set_blob` but omits
`nvs_commit`; that behavior remains explicit. Target allocation zeroes the
temporary capacity-probe buffer, avoiding the OEM's uninitialized heap bytes.
The probe is erased after a successful write. Diagnostic formatting is an
explicit boundary, and invalid size/nonexistent-buffer objects are guarded.

`src/wifi.cpp` implements the ten-state Wi-Fi lifecycle with a concrete Arduino
Wi-Fi adapter. It preserves AP configuration, scan retry, hostname and DNS
setup, station status/error mapping, and connection/disconnection timing.
`ApplicationWifiEvents` forwards network results to the application queue.
The application/backend IP field is the resolved website IPv4 address; the
station's own IPv4 address is a separate field.

`src/http.cpp` implements request/header/body parsing, URI extraction, credential
form decoding, and the original string/prefix/boolean helpers. Concrete ESP32
`Client` and `WiFiServer` adapters implement transport without synthetic network
responses. Route dispatch and page assembly use these concrete transport interfaces.

`src/http_server.cpp` implements every recovered local route, complete Wi-Fi and
data pages, device state JSON and full-test result/history responses. Generated
`web_data.cpp`/`web_wifi.cpp` embed the exact preserved asset literals. The server
preserves the original exact-versus-prefix route matching and explicit client
flush/release ordering. `ApplicationDevice` connects getter/action callbacks to
application identity, requested-program state, controller errors, Wi-Fi address,
sensor readings, actuator controls, pressure history and factory reset. The
route fixture intentionally separates callback dispatch from application state.

`tests/fixtures/core-original.json` records **444 cases executed from original
Xtensa instructions**. The counted assertion helper itself runs, including its
RAM counter update and 200 ms delay call; invalid inputs do not silently stop
at the diagnostic. Effects, returned values and modified state are compared
against compiled source. Clock, GPIO, delay, diagnostic formatting/output and
ROM/libm arithmetic are explicit boundaries. Register-window prologues and
returns are adapted; this is not a complete ESP32/RTOS emulator or hardware test.
`tests/fixtures/hardware-original.json.gz` adds **413 original execution cases**
for the actuator/LED/pump-test modules. `sensors-original.json.gz` adds **534 cases**
for calibration, conversion, I2C transactions and sample metrics.
`controller-original.json.gz` adds **614 integrated lifecycle/controller cases**,
and `application-original.json.gz` adds **454 top-level application/event cases**,
while `storage-original.json.gz` adds **236 NVS/credential cases** and
`wifi-original.json.gz` adds **1,014 Wi-Fi cases** and `http-original.json.gz`
adds **261 HTTP/form cases**, bringing the compared case count to **3,970**.
`http-server-original.json.gz` adds **122 full route/browser cases**, bringing
the total to **4,092**. Complete emitted response bytes, request/form buffers,
device callbacks and flush/release effects are compared. The direct SDK call
`0x400da0ec` was verified as `WiFiClient::flush`; the corrected replay names and
source use that operation rather than a mislabeled stop boundary.
`data-original.json.gz` adds **380 identity/credential/report cases**, including
execution of the original decimal formatter and floating-point classifier.
`backend-original.json.gz` adds **131 request/sync/response cases** and
`update-original.json.gz` adds **91 URL/header/update-state cases**.
`machine-original.json.gz` adds **64 boot/console integration cases**, for a total
of **4,758 original comparisons**. The native embedded self-tests additionally
execute the recovered diagnostic vectors and confirm credential restoration.
Its GPIO/PWM calls, snapshots, counters, complete LED-buffer state, pump-test
state, diagnostic calls and delay effects are checked against native source.
The compressed fixtures are lossless and deterministic (gzip timestamp zero).
The controller fixture compares the complete supplied original object/state
region, including report snapshots and the pressure history, plus callback,
GPIO/PWM, I2C and clock effects. Its 200 ms assertion delays and sensor delays
advance the same clock observed by subsequent original instructions.
The application replay compares the full 8 KiB supplied original state region,
including its nested hardware controller, and checks event ordering and service
calls. Pending button events take priority over pending Wi-Fi events; ordinary
state advancement occurs only when neither pending event is handled.

The OEM logic remains visible: sensor range checks initially allow unqualified
samples, fan RPM must exceed 1600 after five samples at duty >=155, read success
must exceed 0.75, and pump restart uses an inclusive temperature comparison with
a strict pressure comparison. The button uses the original absolute unsigned
time comparisons, including their wrap behavior. These facts are implementation
evidence, not approval of the OEM policy for a new port. The generic thermistor
routine accepts caller parameters; `board_thermistor()` supplies the exact
PicoStill In/Out/Pot/Ambient values. The initial generic arithmetic cases use
explicit test parameters, while the sensor fixture verifies every board
coefficient bit and performs conversions with the original board values.

The actuator port preserves the observed 20 Hz/8-bit PWM setup, the fan's
two-second sampling delay, 50 ms sample intervals, count reset at 600 samples,
and the existing deadline wrap behavior. The LED table's empty AllOff program
executes the original counted assertion and continues. Patterns without a wait
instruction and with infinite repetition remain outside the returning-call
domain; the valid 39-pattern OEM table has been replayed. Bounds-safe lookup of
an invalid animation enum is an owned API guard; the OEM array access assumed a
valid enum and is not claimed equivalent for an out-of-bounds index.

The sensor source preserves the OEM behavior for incomplete I2C replies and
status mismatch: conversion still runs, but invalid readings do not replace
the last accepted value. Temperature and pressure sample-count resets differ
in the OEM and remain distinct here. Hardware setup uses SDA14/SCL27 at 100 kHz,
ADC address 0x34, pressure address 0x18, pump GPIO19, and the constructor's fan
and LED mappings. The concrete `platform/esp32/arduino_ports.cpp` driver calls
the real Arduino GPIO/PWM/Wire/clock/serial SDK. Setup order is reviewed and
target-compiled; its combined setup flow has not been replayed or run on hardware.
Diagnostic formatting/output remains an explicit boundary.

The controller keeps the observed behavior when connection readiness and its
timeout occur together: both independent checks can finish/report in one tick.
Pin-test reset preserves its previous `sensors_passed` flag. A pin-only error
in the full-test aggregation leaves the current error unchanged; the single
pin test reports it normally. Snapshot duty fields update a byte while retaining
the original padding. These quirks are recorded as OEM evidence, not silently
corrected in the reconstruction. Temperature branches use the original ordered
float comparisons, including NaN behavior; no assumption was taken from the
decompiler's inverse comparison text.

The application preserves observed OEM details: a Wi-Fi error during an active
program is ignored, an update finishing in either state zero or six requests
NVS partition erasure before restart, and connected configuration persists the
stored credential record. Manual-menu selection checks the old index before
accessing the new one. The reconstruction additionally rejects out-of-range new
menu indices and a zero menu count; those invalid accesses are excluded from
the equivalent OEM execution domain.

Wi-Fi timing uses the original absolute unsigned deadlines, including their
wraparound behavior. A zero station status during connection returns without
applying the disconnected-status timeout. A completed scan with zero SSIDs
starts an asynchronous retry. DNS failure reports `WifiNoInternet` through the
same callback after returning to Idle. The original AP password and diagnostic
credential reporting are OEM behavior; changing those belongs in `modified/`.

The HTTP parser preserves case-sensitive `Content-Length: ` recognition,
signed-length termination, repeated header handling, 512-byte body saturation,
read/timeout ordering and returning assertions. A one-byte URL-decoder target
triggers the OEM diagnostic and still writes its terminator. Owned guards reject
URI lengths beyond 255, unterminated form bodies, and invalid decoder buffers;
equivalence is not claimed for the original out-of-bounds accesses. The form
parser requires the initial `ssid=` field but accepts any name for the following
password field, as observed in the image.

The route fixture executes original field and browser assembly instructions,
including the entire original pressure-history initialization loop. The runner
adapts Xtensa hardware-loop control while executing each original body
instruction. An independent sentinel probe checks all 804 history/count bytes
and all 199 backedges. Numeric printing and IP/String SDK operations remain
explicit boundaries; target output delegates to the actual Arduino `Client`.
Pressure result counts above the physical 200-entry capacity are bounded in the
owned writer and are excluded from original out-of-bounds equivalence claims.

`src/data.cpp` implements identity arrays, bounded credential copies, MAC/AP
naming and the original XOR/hex credential token. `src/reports.cpp` implements
the sensor CSV, snapshot and full-test diagnostics. The CSV uses Fahrenheit for
the four temperature channels, the raw pressure value, pump state, the **second**
fan state, controller state and error label. The browser fan getter instead
reports the **first** fan; the distinction is retained.

`src/backend.cpp` implements OEM HTTP GET construction, command ownership,
marker-delimited response reading, retry deadlines and update-address handling.
It preserves the original independent `'-'` / `'1'` response-character checks.
`src/update.cpp` implements URL extraction, HTTP status/content headers and all
seven update states. Original accounting ignores short `readBytes`/flash-write
returns and has no body-phase timeout; those behaviors are recorded in the
comparison fixtures, not silently repaired in the OEM reconstruction.
Null/short/no-slash URL accesses and unterminated diagnostics are bounded owned
domains; equivalence is not claimed for original invalid pointer accesses.

`src/machine.cpp` composes all modules, boot ordering, console input and command
dispatch, report callbacks, persistence, restart, factory selection and sleep.
`src/self_tests.cpp` reconstructs the embedded diagnostic vectors. The complete
application uses actual Wi-Fi clients, `Update`, NVS, GPIO/PWM/Wire, serial and
partition APIs through `platform/esp32/`. `application_main.cpp` supplies setup
and loop; the linker includes the unmodified SDK `main.cpp` and its FreeRTOS
loop task. No production service is represented by a successful no-op.
The boot/console fixture compares whole supplied application state and events;
board setup, NVS initialize/load wrappers and OS APIs remain explicit boundaries.
Destructive system wrappers and the embedded self-test runner have source review,
native/target execution evidence where documented, and no full original replay.

Six browser literals in `web/oem/` retain exact original bytes, including CRLF.
They include the data/control page and Wi-Fi configuration UI; HTML is stored as
fragments because the OEM HTTP handler assembles the surrounding response.
The existing browser is evidence for the OEM protocol, not an authenticated v1
standalone application.

## Reproduce

From this directory:

```sh
make verify
python tools/build_esp32_objects.py --toolchain /path/to/xtensa-esp32-elf \
  --arduino /path/to/arduino-esp32
python tools/link_esp32_application.py --toolchain /path/to/xtensa-esp32-elf \
  --arduino /path/to/arduino-esp32
```

The compiler archive and identity are pinned in `dependencies/toolchain.json`;
the development SDK reference is pinned in `dependencies/arduino-esp32.json`.
That SDK pin is not proof of the lost full OEM build environment.
The object step compiles **27 owned modules**. The link step verifies those
source/object hashes, builds the needed SDK core/Wire/Wi-Fi/Update components,
and links all application dependencies using the original SDK linker scripts.
It produces `build/application/picostill-application.elf` and its map, without
a bootloader, partition table or flash installation package. The checked-in
`analysis/esp32-build.json` records source/header/compiler hashes and flags;
`analysis/esp32-application-link.json` records the complete ELF/map, SDK sources,
linker scripts and retained setup/loop/service symbols. Native verification
checks that these reports still describe the current checked-in source.

For independent original execution, install the pinned Ghidra/JDK described in
`tools/firmware_analysis/README.md` at the repository root. From the repository
root, import the image:

```sh
python tools/firmware_analysis/ghidra_export.py \
  --input firmware/picostill/oem/upstream/picostill_0_0_35.bin \
  --layout firmware/picostill/reverse-engineered/analysis/ghidra-layout.json \
  --output /tmp/picostill-export --project-output /tmp/picostill-project \
  --ghidra-home /path/to/ghidra --java-home /path/to/jdk
```

Replay original functions against the deterministic case file. Use absolute
paths for the Ghidra script and fixture arguments:

```sh
JAVA_HOME=/path/to/jdk /path/to/ghidra/support/analyzeHeadless \
  /tmp/picostill-project firmware -process picostill_0_0_35.bin \
  -noanalysis -readOnly \
  -scriptPath /absolute/repository/firmware/picostill/reverse-engineered/tools/ghidra \
  -postScript EmulatePicoStillCore.java \
  /absolute/repository/firmware/picostill/reverse-engineered/tests/fixtures/core-inputs.json \
  /absolute/repository/firmware/picostill/reverse-engineered/tests/fixtures/core-original.json
```

Use `EmulatePicoStillHardware.java` with `hardware-inputs.json.gz` and
`hardware-original.json.gz` in the same fixture directory to reproduce the
hardware-module replay. Ghidra reads and writes gzip directly; the archive
format does not replace or shorten the executed original continuations.
Use `EmulatePicoStillSensors.java` with `sensors-inputs.json.gz` and
`sensors-original.json.gz` for the sensor/calibration replay. It needs the
current seeded layout, including the static initializer entry.
Use `EmulatePicoStillController.java` with `controller-inputs.json.gz` and
`controller-original.json.gz` for the integrated controller/hardware lifecycle.
Use `EmulatePicoStillApplication.java` with `application-inputs.json.gz` and
`application-original.json.gz` for top-level state/events and nested execution.
Use `EmulatePicoStillStorage.java` with `storage-inputs.json.gz` and
`storage-original.json.gz` for NVS record, heap-failure and persistence behavior.
Use `EmulatePicoStillWifi.java` with `wifi-inputs.json.gz` and
`wifi-original.json.gz` for Wi-Fi transitions, callbacks, SDK call order and
timeout/wraparound cases. SDK association and radio hardware are boundaries.
Use `EmulatePicoStillHttp.java` with `http-inputs.json.gz` and
`http-original.json.gz` for HTTP/parser/form execution. Its larger instruction
budget permits full oversized-header and saturated-body cases; the executed
instructions and diagnostics are not replaced with reconstructed parser logic.
Use `EmulatePicoStillHttpServer.java` with `http-server-inputs.json.gz` and
`http-server-original.json.gz` for full route/browser output and nested parsing.

Use `EmulatePicoStillData.java`, `EmulatePicoStillBackend.java`,
`EmulatePicoStillUpdate.java` and `EmulatePicoStillMachine.java` with their
matching `data-`, `backend-`, `update-` and `machine-` gzip input/output files for
the additional identity, transport, updater and composition comparisons.

`analysis/source-evidence.json` binds reviewed entry ranges to actual OEM bytes
and source/replay files. `tools/record_source_evidence.py --export
/tmp/picostill-export` refreshes it only after reviewing changes and rerunning
original/native comparisons. The seeded export discovered 3,015 functions
without decompiler failures; unreviewed decompiler output is not claimed as
buildable or correct application source.

## Remaining integration

The modified authenticated v1 API/browser/Hub operating model remains a separate
implementation step. This OEM component now has complete application composition
and SDK link evidence. Direct original comparison of destructive system wrappers
and the entire embedded self-test runner remains outside the verified domain.
No device flash layout, physical installation, boot, actuator or recovery
qualification is claimed.

Source images and hashes come from the repository's unchanged
`firmware/upstream-manifest.json`. Container parsing follows the
[Espressif image format](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/firmware-image-format.html).
All six PicoStill containers pass XOR and appended SHA-256 checks; those checks
are integrity checks, not publisher signature verification or board compatibility.
