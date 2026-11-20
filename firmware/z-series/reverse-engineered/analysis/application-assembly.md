# Remaining Z application assembly

This is the continuation map for the Z119 OEM reconstruction. Read it with
the module/evidence table in `../README.md`. Recovered components and SDK
object compilation do not yet constitute a linked application or standalone
firmware. Keep OEM behavior in this directory; make the subsequent standalone
changes under `../../modified/`.

## Services now available

The portable source includes thermal acquisition/control/interlocks, MSP
protocol, program execution/storage/recipe parsing, Core startup/flow/commands,
Wi-Fi, Runtime and Core mailboxes, incoming HTTP parsing/connection handling/
state/recipe publication, outgoing HTTP, PicoStill control/protocol/sensors,
display assets/rendering and substantial Head event handling. The buzzer/alarm
and System watchdog/timing now have original-execution comparisons and real
historical SDK bindings. Physical inputs supply a separate producer layer;
the existing `head_encoder` module consumes its events.

The previously saved `local_http_application` draft is now verified and
documented in `local-http-application.md`, including six explicit bounded
input-validation deviations from completed OEM execution. Its old WIP branch
is a historical checkpoint, not an additional implementation to merge.

## Next unrecovered application bodies

The addresses below are entry points in the preserved 0.0.119 image, not
invented APIs or verified implementations. Their owned services still need
to be recovered and independently checked before application assembly can
bind them.

| Area | Original entry points | Required result |
| --- | --- | --- |
| Head Core-event consumer | `0x401095b4`; state presentation `0x40106e84` | Consume the existing complete `CoreEvent` payload and reproduce recipe results, errors, program readiness and Core state presentation. |
| Head UI-command dispatcher | `0x401080d4` | Reproduce menu/action routing, input flags, screen selection and typed Core commands. |
| Head Still mailbox | `0x400f41ec` | Bind the existing `CoreStillEvent` and preserve the original one-slot read behavior. |
| Screen/widget composition | Callees of the Head dispatchers and screen constructors | Construct and render actual widgets; current catalog, renderer and periodic widget callbacks alone are not a complete front panel. |
| HTTP backend | Tick `0x4011c30c` and its request/result callees | Implement scheduling, response interpretation and publication through the existing outgoing HTTP and Core-event modules. |
| Registration | Tick `0x400fb100` and its request/result callees | Preserve registration completion/error state and event publication. |
| Program download | Tick `0x4010ca58` and its download/parser callees | Bind concrete download ownership and completion to the recovered recipe builders. |
| Session logger | Tick `0x4011c928` and lifecycle callees | Implement session creation, state transitions, persistence and transmission. |
| Azure/group session | Tick `0x400f400c` and lifecycle/client callees | Supply the service whose events the recovered Head network handler already consumes. |
| HTTP console | Registration `0x400f7dc8` | Install `i`, `start` and `stop` in the real command registry. |
| Other service/platform objects | Core startup/tick stage lists | Finish firmware/binary update services, Ethernet/TLS client selection, serial logging/command registry and all remaining required operations. |

## Composition requirements

1. Own every component and callback binding for its actual lifetime. Local HTTP
   application/network objects, Still HTTP bindings, diagnostic/console sinks
   and timer callbacks must remain valid while registered.
2. Refresh `CoreFlowObserved` and `HeadFlowObserved` from live component state at
   the original tick stages. The current structs are snapshots, not automatic
   links to their sources.
3. Copy transient Head command data into an owned Core command envelope before
   queueing it. `CoreCommand::detail` must not retain a pointer into a callback's
   local object. Preserve the established drop-new behavior of occupied
   mailboxes.
4. Serialize the original task/timer/interrupt accesses deliberately. Portable
   instruction fixtures are sequential; they do not prove RTOS synchronization
   or the safety of executing an arbitrary callback from an ISR.
5. Supply every startup stage, tick stage and effect. Missing operations must
   remain explicit failures until implemented; successful no-op substitutes
   cannot establish an OEM rebuild.
6. Add the application owner and entry point, then perform a complete target
   link and package only against an evidenced flash layout. The historical
   object archive does not select partitions or provide installation support.

## Evidence and hardware boundaries

The original assertion helper `0x400db014` logs, increments the shared counter,
waits 200 ms and **returns**. An assertion call alone is not evidence that the
original caller stopped. Execute the continuation when claiming complete
behavioral parity. Where the owned source deliberately rejects an input,
record the actual original continuation and describe the rejection as a
reconstruction policy difference. Historical observations stopped at the
first assertion cover only that prefix, not complete original execution.

The initial baseline is preserved in `source-checkpoint.json`; later module
reports record their own source hashes and original-execution fixtures.
Its 30-module `esp32-object-build.json` remains historical evidence; the current
59-module build is recorded separately in `integration-target-build.json`.
Changes to source covered by those reports require revalidation and fresh
source-bound evidence. The combined native gate is `make verify`; target
interfaces are checked by `tools/build_esp32_objects.py` with the pinned SDK,
compiler and JSON dependency.

Neither application image establishes a complete bootloader/partition backup
or the physical Z1/Z2 mapping. Do not infer compatibility from the file name or
report source/host/object verification as physical boot, brewing, flashing or
OTA validation. A complete OEM application precedes the modified standalone
API, device browser and Hub feature port.
