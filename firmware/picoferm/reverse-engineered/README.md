# PicoFerm OEM reconstruction

`tools/analyze_images.py` verifies all 16 preserved images against the unchanged
upstream manifest and parses both the eboot and application containers. Run it
with `--check` to verify `analysis/image-inventory.json` and the reproducible
`analysis/ghidra-layout.json` for 0.2.7.

The latest image is 322,544 bytes, SHA-256
`08c34af891036494ae81d6f29e7495a475f67be977aa4effd3adde6fee4e9532`.
It is an **ESP8266/LX106** image, not an ESP32 image. The eboot entry is
`0x4010f29c`; the application's E9 header is at file offset `0x1000`, with entry
`0x40100004`. Both XOR checksums and the inter-image `0xaa` padding are checked.
The archive has no appended image SHA-256 or verified publisher signature.

The embedded SDK paths identify Arduino ESP8266 **2.4.1**. Automatic Ghidra analysis discovered 1,872 functions. The application export
seeds three additional callback/vtable/static-initializer entries, producing
1,875 functions without decompiler failures or an analysis timeout. Both
exports are recorded in `analysis/ghidra-analysis.json` and
`analysis/application-ghidra-analysis.json`; these are analysis records,
independent of the instruction comparisons and target build below.

## Recovered application source

`include/picoferm/core.hpp` and `src/core.cpp` recover these original behaviors:

- The packed 236-byte EEPROM application record, version migration, blank
  reset, pressure calibration validation, and 12-record measurement ring.
  Unrelated record bytes remain opaque and are preserved exactly.
- The session response parser, including extra commas, invalid suffixes and
  32-bit integer overflow. Wire code **2 means Idle** and **10 means Fermenting**.
  Session error flags are retained independently of the state.
- The GPIO 15 latch around the GPIO 13 button read, session/button persistence,
  and ADC event handling: calibration mode stores the pressure offset; running
  state 4 records temperature and calibrated pressure.
- Both diagnostic assertion forms yield twice and return in this image.
  Invalid slopes therefore continue into the original pressure arithmetic.
  A zero-over-zero result retains the original positive NaN bit pattern in
  the stored record, rather than depending on a host CPU's NaN sign.

`make verify` builds a native library and compares **4,066 independent cases**
with Ghidra execution of the original instructions. The replay executes the
assertion helpers, original newlib decimal conversion, soft-float comparisons,
multiply and divide. ROM conversion/subtraction/division, diagnostic logger,
GPIO and EEPROM commit are explicit external boundaries. Every case compares
the full 236-byte record where applicable, session/error results and observable
boundary effects. The complete main scheduler now executes in 660 application cases. Its
inbound HTTP poll is an explicit boundary in that suite and executes in
440 separate HTTP cases; live radio and peripheral behavior are not emulated.

The compressed fixtures use deterministic gzip timestamps. Their compressed
hashes, the original input hash and reviewed function byte ranges appear in
`analysis/source-evidence.json`; the output fixture also hashes its input
fixture. Regenerate inputs with `python3 tools/core_cases.py`. To regenerate
original results, open the pinned Ghidra project produced using
`analysis/ghidra-layout.json`, then run `tools/ghidra/EmulatePicoFermCore.java`
with the absolute input and output gzip fixture paths as its two arguments.
`tools/record_source_evidence.py --export <export-directory>` records byte
ranges from the same original image and refreshes source/replay identities.

Three explicit owned guards have no successful OEM parity claim: null session
input returns false after the assertion instead of dereferencing null; invalid
physical log indices are rejected; the target driver's diagnostic temporary
can hold extreme float formatting without reproducing the original buffer
overflow. These distinctions are recorded in source and tests.

## Recovered hardware and Wi-Fi

`hardware.hpp` / `hardware.cpp` reconstruct the original 24-bit ADC register
commands and blocking ready/calibration waits. The concrete Wire adapter uses
SDA 2, SCL 14 and address `0x2a`. Acquisition switches pressure and thermistor
channels over 13 states, discards settling samples, stores pressure at state 6,
and publishes the temperature/pressure pair at state 12. The original float
rounding points, thermistor logarithm, beta constant 3428, 10 kΩ reference and
Fahrenheit output are preserved. **889 original instruction cases** check all
register effects, signed samples, acquisition transitions, battery conversion,
and LED selection. The original newlib logarithm executes in the replay.

The LEDs use pins 4, 5 and 12. Ten original patterns cover Off, Access Point,
Updating, Busy, Talking to PicoBrew and fermentation/account/error states.
Console numbers omit Updating: text `2` selects Busy. The active-low button is
on pin 13 with a pin 15 latch when reading its persisted wake state; a held
button emits one long-press event strictly after 2 seconds. The complete button and LED scheduler now executes in the main application
comparison, including held/released inputs, deadline equality, rotating and
finite LED patterns, and returning diagnostics.

`wifi.hpp` / `wifi.cpp` implement all eight original Wi-Fi states, including
asynchronous scanning before setup AP mode, station association, DNS validation,
first-error retention, and the 2-second AP flush delay. The original AP address
is `192.168.4.1` and password is `PicoFerm12!`. **775 original instruction cases**
check exact state, errors and SDK-call order, including return from diagnostic
assertions and unsigned deadline wrap. Actual SDK sources distinguish
`setAutoConnect(false)` from `persistent(false)`; both occur before Wi-Fi is
disabled. Physical association, RF, sensor quality and battery life remain
unverified.

## Identity, credentials and backend

`data.hpp` / `data.cpp` recover the five identity arrays, hexadecimal MAC/UID,
AP name and XOR-derived token. Credential updates retain the original bounded
copy diagnostics, clear the has-Wi-Fi marker, and set the application change
flag. Storage reporting walks the original ring in order and masks password
text at the diagnostic boundary. **300 original cases** compare complete
identity and EEPROM records, bounded output bytes and diagnostics; the original
SDK number formatter executes inside the replay.

`backend.hpp` / `backend.cpp` implement the entire original sync transaction:
registration, firmware check, optional firmware address, session state and
optional 12-sample history upload. The concrete TCP adapter makes up to four
connection attempts separated by 500 ms, keeps the first transport error and
stops the client before dispatching completion. **216 original cases** compare
full backend state, outgoing requests and ordered effects, including multi-step
transactions, short/overflow responses, marker framing, disconnects and
unsigned deadlines. The backend never contacts a live service during tests.

The backend URL field preserves all 128 original bytes; diagnostics use a
separate terminated view. Extreme float diagnostics have larger temporary
buffers than the OEM to prevent its stack overwrite. These owned bounds are
explicit and do not claim parity for invalid original memory accesses.

## Complete application and setup browser

`application.hpp`, `application.cpp`, `console.cpp` and `self_tests.cpp`
implement all twelve main states, serial startup, the diagnostic command
dispatcher, embedded component tests, EEPROM load/report/reset, callbacks,
update results and sleep. The factory reset retains pressure offset, its
calibration marker and pressure slope. Update success requests a restart;
failed/no-update results return to the caller. Sleep preserves the original
32-bit seconds-to-microseconds multiplication before passing the result to
the SDK's 64-bit argument. No UART receiver is invented: the OEM loop consumes
an already queued command, and the owned `queue_console` entry makes that
boundary explicit.

**660 original application cases** compare owned state, EEPROM storage and
ordered effects. They execute the original ADC/Wi-Fi/backend helpers,
assertions and embedded diagnostic vectors. TwoWire's opaque SDK object is
normalized in these comparisons: the OEM assignment copies an uninitialized
SDK timeout-start word. This does not omit any acquisition state, sensor
result, EEPROM field, actuator output or application flag.

`http.hpp`, `http_server.hpp`, their source and preserved HTML asset implement
the original access-point setup, details, credentials, factory-reset and
pressure-calibration pages. **440 original HTTP cases** compare requests,
response bytes, credentials and effects. DNS polling and yield occur even when
HTTP handling is disabled. The parser timeout is 1,000 ms, recovered from the
constructor. Oversized original request/URI/body writes are explicitly rejected
by owned bounds; those inputs are not labeled successful OEM parity cases.
See `analysis/http.md` and `analysis/http-validation.json`.

The concrete `platform/esp8266/application_main.cpp` composes every recovered
module with real SDK adapters. The original Arduino startup calls owned
`setup` and `loop`; the HTTP virtual device delegates to that same application,
so calibration and credential actions affect the live owner.

## Target compilation and platform boundary

`platform/esp8266/` provides concrete Arduino EEPROM get/put/commit, GPIO,
yield, Wire, battery ADC, Wi-Fi, TCP, delay and exact SDK formatter bindings. Its diagnostic formatter performs
the original seven yields; no UART write survives in the original logger body.
The SDK implementation and driver calls are reviewed and target-compiled
separately from the explicit replay boundaries.

Public dependency packages are pinned by archive SHA-256 and file identity
under `dependencies/`. They are reference dependencies, not a claim that all
vendor build options are known. Compile all seventeen owned target modules
and link the complete application with:

```sh
python3 tools/build_esp8266_objects.py \
  --toolchain /path/to/xtensa-lx106-elf \
  --arduino /path/to/esp8266-2.4.1
python3 tools/link_esp8266_application.py \
  --toolchain /path/to/xtensa-lx106-elf \
  --arduino /path/to/esp8266-2.4.1
```

The verified target compiler is Xtensa LX106 GCC **4.8.2**. The checked reports `analysis/esp8266-build.json` and
`analysis/esp8266-application-link.json` record object and complete-link evidence.
The retained SDK startup, owned setup/loop, application scheduler, console,
HTTP server, Wi-Fi and backend symbols are checked after linking. This build
has 311,548 text bytes, 8,872 data bytes and 33,408 BSS bytes; these measurements
do not measure live stack usage or heap peaks. `make verify` runs seventeen
native tests, original-case/source identity checks and all 16 OEM image checks.

Additional owned guards reject invalid ADC states and LED indices before
out-of-table access and keep null network credentials as empty strings after
the diagnostic. Successful OEM parity is not claimed for those invalid inputs.
The original blocking ADC waits remain explicit; modified firmware can apply
an independently documented timeout policy without rewriting this OEM source.

## Reproducing the application evidence

Run `python3 tools/application_layout.py --check` to verify the added Ghidra
entry points, or omit `--check` to regenerate them. Import the unchanged
0.2.7 image with the root `tools/firmware_analysis/ghidra_export.py`, using
`analysis/application-ghidra-layout.json` as the layout. Then regenerate
`tests/fixtures/application-inputs.json.gz` with
`python3 tools/application_cases.py` and run
`tools/ghidra/EmulatePicoFermApplication.java` in that project with the absolute
input and output fixture paths. The runner rejects unmodeled calls,
instruction gaps, exhausted input and instruction/depth limits. Its successful
660-case completion is independent of the native comparison.

## Remaining product and physical work

The recovered OEM application composition and SDK link are implemented.
The modified authenticated local API, standalone browser and Hub experience
are being added separately under `modified/`; OEM reconstruction alone does
not establish S/Pro product parity.

The link tool uses the original SDK's 2-MiB reference linker layout, consistent
with the 0.2.7 image header. That header does not establish any particular
unit's complete flash contents, filesystem, recovery path or installation
procedure. The tool produces **ELF/map only**, with no flash package. Physical
sensor accuracy, RF, battery behavior, calibration, installation and recovery
remain unverified. OEM bytes and archive hashes are unchanged.
