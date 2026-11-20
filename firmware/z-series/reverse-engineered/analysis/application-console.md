# Core application console

`ApplicationConsole` owns the recovered Core shell hierarchy and routes its
commands into the composed services and diagnostic bodies. Its 23 shells
contain 230 commands and 22 child links. Shell names, capacities, exact/prefix
matching, token/description spelling, insertion order and original callback
addresses come from execution of the 21 original registry functions in Z119.
Registration is idempotent so component setup preserves its original order.

## Implementation and ownership

The module owns persistent entry storage, callback contexts and child shells.
It uses `ConsoleShell`, whose registration, dispatch, help and serial-input
behavior is separately compared with the OEM. `register_component(CoreSetupStage)`,
`register_heater()`, `register_max11613()` and `register_display()` preserve the
component boundaries. Buzzer and EEPROM registration use their setup services.

Ordinary callbacks cover Wi-Fi targets and credentials, HTTP enablement, Buzzer,
EEPROM, physical encoder events, MSP/servo operations, safeties, heater/PID,
ADC polling/detection, PicoStill and Runtime commands/information, backend and
session information, SerialLogger, Azure lifecycle, Core commands/events and
owned Azure test-session publication. Platform and Head callbacks dispatch to
their concrete SDK or presentation owners. Diagnostic commands use the bodies
listed below. A missing or failed required binding returns `ServiceFailure`;
forwarding an address alone is not evidence that its target body executes.

The hooks provide address-based SDK/Head dispatch, raw Arduino Print output,
clock/counter/stack-water readings, yielding, delays, watchdog reset, the suite
message renderer and a dedicated HTTP test transport. Ordinary `scope` tracing
and `test_scope` are distinct: the latter binds the original TestScope watchdog,
stack-water and assertion-result behavior. Platform information logs use the
OEM IV form with no source file/line; other logs retain their original level,
tag, source basename and line. Hooks and referenced services outlive the console.

Runtime stop/pause immediately stop the heater and send the pump mask before
posting their Runtime command. Core and component mailboxes retain an already
queued value when full. Servo arguments narrow to a byte, and their warning
precedes transmission. Disabling safeties stops/logs the three monitors in
original order. Heater duty assertions return before clamping. `setduty` is
registered as an exact token in the OEM: shell input with a numeric suffix
fails to match, while direct callback cases exercise the otherwise inaccessible
numeric-body branches. This original quirk is preserved.

## Recovered diagnostic commands

| Area or command | Original entry | Implementation and separate evidence |
| --- | --- | --- |
| EEPROM and MAX11613 | `0x400dd678`, `0x400de068` | Complete temporary-data and thermistor test bodies; [hardware diagnostics](hardware-console.md). |
| MSP version/switch/update/integration | `0x400dc1a4`, `0x400dc970`, `0x400dd268`, `0x400dd29c` | Actual controller transaction and recovery sequences; [MSP continuation](msp-continuation.md). |
| Runtime `t` | `0x400e0e70` | Instruction stack, drop monitor and valve enum tests; [software diagnostics](software-console.md). |
| PicoStill `test` / `t b` | `0x4011a204`, `0x4011a1fc` | Sensor, statistics, protocol and authorization tests; [software diagnostics](software-console.md). |
| HttpBackend / ZBackend data tests | `0x40119378`, `0x4011ec38` | Production JSON/program operations, reports and encoders; [backend diagnostics](backend-console.md). |
| Basic | `0x4011ed44`, wrapper `0x4011fbac` | All 37 leaves, original group order, nested diagnostics and final Head callback; [Basic diagnostics](basic-console.md). |
| Live HTTP | `0x40110f80` | Eight original request scenarios through the actual asynchronous client and dedicated transport; [HTTP diagnostic](http-client-console.md). |
| Live HttpBackend | `0x4011b6cc` | Nine request/parser phases through the actual HTTP, JSON and program implementations, with UID restoration; [live backend diagnostic](http-backend-network.md). |
| Azure integration/password | `0x400fec74`, `0x400fa684` | Real lifecycle/wait/TestScope logic and original signature vectors; [Azure diagnostics](azure-console.md). |
| BinaryDownload | `0x401024a8` | Original 16-byte test specialization and completion checks; [Network diagnostics](network-console.md). |
| Network / integration / All | `0x4011c2f8`, `0x4010a3d8`, `0x4011fbbc` | Actual marker, interface selection and ordered child dispatch; [Network diagnostics](network-console.md). |
| Display and Head | Original callback manifests | Real graphics, full display demonstrations, StabilityText and Head painters; [Display console](display-console.md), [Head application](head-application.md). |

The registry has no remaining unassigned diagnostic callbacks. The live
HttpBackend proof completes 12 response cohorts through 179 original bodies,
comparing 22,810 ordered events and 231 returning assertions. It covers the
actual request/client/receiver/parser/program path, including error responses,
wrapping counters/clocks and UID restoration. Socket/SDK operations and the
first-event mailbox remain explicit boundaries. The committed traces hash
request, response, UID and diagnostic bytes instead of exposing private data.

The 64-case Network/Int/All/Binary comparison executes the aggregate bodies
with explicit typed boundaries for child diagnostics, including their modeled
elapsed time and returning assertions. It establishes ordering and aggregate
observations, not a monolithic nested execution of every suite. Individual
body proofs remain separate. BinaryDownload supplies zeros for the original
uninitialized and unobserved local payload; the compared observable state and
diagnostics do not assert equality of undefined payload bytes.

`ConsoleTestMarker` implements the original category/result lifecycle,
assertion-count snapshot, wrapping elapsed time, colored result output and
returning failure assertion. Its display callback uses the actual centered
6×8-font renderer and restores the persistent text context. It is covered by
[backend marker](backend-console.md) and [display](display-console.md) comparisons.
The Basic fixture keeps the final Head callback and marker drawing as explicit
boundaries; the application binds them to the actual Head and Display owners.

The original helper at `0x400db014` logs, increments the counter, delays 200 ms
and returns. Diagnostic tests retain that continuation. Missing callbacks or
callbacks that reject their operation are separate source integration guards;
they are not treated as ordinary OEM assertions. The two malformed ZBackend
integer-as-float logs have narrowly documented output corrections. Historical
null-diagnostics MSP behavior and malformed recipe/program input policies retain
their explicit limits in [assertion coverage](assertion-coverage.md).

## Execution evidence

[Registry execution](application-console-registry-execution.json) records
21 registry cases and 49 original functions. Original Shell constructors and
ownership moves execute; generic command/child insertion is an explicit typed
capture boundary covered separately by the generic-console evidence. The
native hierarchy matches all 252 entries and all 23 names/capacities.

[Ordinary callback execution](application-console-command-execution.json)
records 297 cases through 62 original functions. The actual assertion helper
executes 25 times, including its counter update and 200 ms delay, before each
caller continues to return. Cases cover state changes, safety/Wi-Fi logging,
Runtime command construction and exact promoted binary32 payloads, occupied
mailbox retention, PicoStill queue selection and servo transmission requests.

Runtime/PicoStill mailbox insertion and MSP transmission are typed capture
boundaries in that ordinary-callback fixture. The native side calls the actual
composed implementations. This establishes callback payload/order behavior;
it does not replace separate mailbox, transport or device evidence. C integer
conversion, scalar arithmetic, formatting and prescribed clock/delay are
explicit modeled environmental boundaries.

Each diagnostic document above records its additional complete-body comparisons.
In particular, the software suite covers 14 cases, backend data/markers cover
47, Basic covers 152, live HTTP covers 55, live HttpBackend covers 12, Azure
covers 40 and the Network/Int/All/Binary suite covers 64. Aggregate replay
and leaf replay have different boundaries and may overlap; their totals are
not a count of unique paths or a monolithic firmware execution.

The native tests also check source/fixture identities, exact-token dispatch,
immediate stop/pause effects, nonzero MSP boot-version output and required
external-service forwarding. [The console validation report](application-console-validation.json)
records the checked callback/dispatch inventory. The [full object report](integration-target-build.json)
and [application link report](application-link.json) identify the complete
checked target composition; older module build reports remain scoped historical
evidence. The native and original-instruction comparisons do not contact live
cloud services or establish physical hardware, brewing, flash installation or
OTA qualification.

## Reproduction

From the `reverse-engineered` directory, with the selected ArduinoJson checkout:

```sh
ARDUINOJSON_DIR=/path/to/pinned/ArduinoJson python tools/validate_application_console.py --write-report
ARDUINOJSON_DIR=/path/to/pinned/ArduinoJson python -m unittest discover -s tests -p test_application_console.py -v
```

Run `make verify` for the complete native suite. Azure's independent HMAC oracle
also requires the OpenSSL development package (`libssl-dev` on Ubuntu), which
the reconstruction CI installs. Each diagnostic document gives its focused
validator and original-execution reproduction commands.

The two `tools/ghidra/EmulateApplication*.java` scripts replay the original
registry/ordinary callbacks using the shared `Z119Execution.java` helper.
Their reports pin the image and input bytes. The separate diagnostic runners
use isolated original-image projects and deterministic compressed fixtures;
credential-bearing text is hashed or generated privately where required by
those reports. These reproduction commands operate on files, not appliances.
