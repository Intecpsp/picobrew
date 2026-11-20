# Z119 application assembly and evidence boundaries

The concrete OEM application is assembled and links against the pinned
historical ESP32 SDK. Its Core/Head service graph, peripheral/network owners,
screen painters and recovered diagnostic bodies are implemented. The separate
[modified profile](../../modified/README.md) composes these controls with the
standalone API, storage, browser and Hub integration.

## Integrated composition

| Area | Owned result and evidence |
| --- | --- |
| Core | Real component ownership, one shared Head mailbox, owned command/Azure payloads, original startup/tick order and live state synchronization; `core_application.*` and its executable integration tests. |
| Head | Fourteen event queues, input editors, Core/UI/Runtime/Still/network consumers, recurring screens and actual painters; [Head application](head-application.md). |
| Display | Original grayscale display, fonts, graphics, numeric/text/quantity/menu widgets, hardware/Still/status views, slideshow pages and console demonstrations; [screens](head-screens.md), [presentation](head-presentation.md), [slideshow](head-slideshow.md), [console](display-console.md). |
| Backend and sessions | HTTP request/result state machines, registration, recipe download, session creation/logging, serial CSV and Azure/group-session processing; [backend services](backend-services.md), [diagnostic mappings](backend-diagnostics.md), [Core helpers](core-helpers.md). |
| Physical platform | GPIO, interrupts, timers, buses, screen, EEPROM, network clients, TLS, MQTT, update and watchdog adapters; [concrete application](application-platform.md). |
| Serial commands | Original shell hierarchy, ordinary callbacks, diagnostic test bodies and concrete platform/Head hooks; [application console](application-console.md). |
| Target | Full recovered-module and dependency-object inventory in the [object report](integration-target-build.json), with separate [complete SDK link evidence](application-link.json). Source/header/object identities and required symbols, rather than a fixed prose module count, identify each checked build. |

The owner supplies every normal startup stage, tick stage and control effect.
A missing or failed required service remains an explicit integration failure;
a successful empty callback is not used as evidence that the operation exists.

## Diagnostic bodies and returning assertions

The original assertion helper logs, increments the diagnostic counter, delays
200 ms and returns. The newer comparisons retain that helper and the caller's
subsequent work. TestScope also has its actual clock, assertion-count,
stack-water and watchdog bindings; it is separate from ordinary debug scopes.

| Diagnostic or continuation suite | Implemented coverage and evidence |
| --- | --- |
| EEPROM and MAX11613 commands | Complete temporary-data tests and 32,656 thermistor conversions; four original cases with seven returning assertions. [Hardware diagnostics](hardware-console.md). |
| MSP commands, recovery and diagnostics | Production setup/update/error continuation, application-version reads, boot/application switching and update/integration tests; 123 complete original cases and 320 returning assertions. [MSP continuation](msp-continuation.md). |
| Runtime and PicoStill diagnostics | Actual instruction-stack, sensor, statistics, protocol and authorization test bodies; 14 original cases and 39 returning assertions. [Software diagnostics](software-console.md). |
| HttpBackend and ZBackend diagnostics | Actual JSON/program operations and reports, event encoders and shared suite markers; 47 original cases and 49 returning assertions, with two specifically documented malformed OEM log differences. [Backend diagnostics](backend-console.md). |
| Basic aggregate | All 37 leaf bodies and the complete aggregate/wrapper, including the reused hardware, Runtime, Still and backend tests; 152 original cases and 651 returning assertions. [Basic diagnostics](basic-console.md). |
| Live HTTP diagnostic | Dedicated client resources and the production asynchronous request/response engine; 55 original scenarios, 511 returning assertions and explicit socket/environment boundaries. [HTTP diagnostic](http-client-console.md). |
| Live HttpBackend diagnostic | All nine request/parser phases, actual HTTP client/receiver/JSON/program bodies and UID restoration; 12 exact response cohorts, 22,810 ordered events and 231 returning assertions. [Live backend diagnostic](http-backend-network.md). |
| Azure integration/password diagnostics | Signature generation and actual lifecycle/backoff/wait/TestScope bodies; 40 complete original cases and 66 returning assertions, with independent HMAC implementations at the explicit crypto boundary. [Azure diagnostics](azure-console.md). |
| Network, Int, All and BinaryDownload | Aggregate marker/interface/child-call order and the original 16-byte BinaryDownload test; 64 exact cases and 148 returning assertions. Child diagnostic bodies are explicit typed boundaries in this orchestration replay, with their own leaf proofs. [Network diagnostics](network-console.md). |
| Display and Head tests | Actual painters and all original Display callbacks/demonstrations; the category/result renderer, returning empty-text assertion and persistent context are compared separately. [Display console](display-console.md), [Head application](head-application.md). |

The aggregate comparison establishes child order, markers and shared state;
the separate live HttpBackend proof executes its nested HTTP/JSON/program path.
Socket and SDK operations, formatting and the first-event mailbox remain
explicit boundaries in that leaf proof. Its recorded request, response, UID
and diagnostic bytes are hashed before public output. No live vendor request
is made by these comparisons.

Production continuation evidence also completes all 5,792 Runtime cases,
2,363 EEPROM cases, 6,480 Still controller/client cases and 972 outgoing HTTP
cases. These suites overlap other fixtures; their totals are not a count of
unique original paths. See [assertion coverage](assertion-coverage.md) for the
complete evidence classification.

The historical repository/runtime/MSP fixtures retain their original
assertion-stopped prefixes. Newer full-return suites do not silently relabel
those artifacts. The null-diagnostics MSP interface remains deliberately
guarded, and the recipe/program parsers, some character-selection cases and
selected malformed Still/backend inputs retain documented bounded behavior.
The two malformed ZBackend vararg logs are corrected explicitly. Each module's
report distinguishes exact continuation from a defined source deviation.
The BinaryDownload self-test supplies zero bytes for the OEM's uninitialized,
unobserved local payload; its counts, copies and completion are compared, not
undefined payload bytes.

## Implemented standalone profile

`../../modified/` owns the authenticated API, local recipe catalogue, persistent
settings and history, optional asynchronous Hub forwarding, capability-driven
browser and native front-panel integration. The actual recovered Core, Head,
Runtime and shared Engine are exercised together by its integration suite.
The profile retains native preparation prompts, uses direct valve locations
1–6 for canonical recipes, and does not reinterpret archived native Z grammar
as canonical API input.

The modified owner adds latched output inhibition, bounded controller transport,
sensor-I/O cutoff, verified checkpoints and explicit interrupted-brew recovery.
It starts locally without vendor registration or a cloud/DNS prerequisite.
Its advertised transport is Wi-Fi, and its settings menu omits unsupported
cloud/Ethernet configuration. The device browser and Hub use the advertised
capabilities and expose the actual grayscale display and ordered encoder input.
See the [standalone operating and build contract](../../modified/README.md)
and [cross-family parity matrix](../../../../docs/firmware-parity.md).

## Verification and installation boundaries

`make verify` builds the host archive, runs native original-fixture and
integration tests, and checks the preserved image reports. The target builder
compiles the owned modules against the hash-pinned compiler, SDK, JSON and
Ethernet dependencies. The application linker rejects stale source/header/
object/archive identities and checks required lifecycle and internal-memory
symbols. The reconstruction CI runs both host and target paths; the standalone
CI separately checks its actual controller/Engine integration and reference
image package.

`source-checkpoint.json` and `esp32-object-build.json` preserve the initial
historical checkpoint. The full object/link reports identify the revision
actually compiled; a historical report is not a current source pass/fail gate.
Section sizes and inventory counts should be read from that build's reports.

The available originals do not establish physical Z1/Z2 mapping, a complete
device bootloader/partition backup, or a Zymatic application image. The modified
package uses an explicit custom reference layout, not a recovered device layout.
No reconstructed image has been installed or used to operate a brewer. An
ELF/map link, instruction comparison, host test or package check is not physical
boot, brewing, flashing or OTA installation/recovery validation.
