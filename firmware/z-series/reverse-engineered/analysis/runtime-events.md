# Runtime, Core and Head event payloads

The original Runtime event has four independent application fields: event type,
previous state, current state, and notification. The notification is at original
event offset `+0x10`; it is not one of the transition states. Core forwards this
event to Head, whose notification handler reads that fourth field.

The first Core reconstruction deliberately captured only its transition fields
at the forwarding service boundary. `CoreRuntimeEvent` now preserves the complete
payload. `runtime_events.hpp` provides typed adapters for the already recovered
Runtime service callback and the Head event handler. The Core mailbox owns the
notification together with the transition state; a second event cannot overwrite
it while the first is pending.

## Independent evidence

`EmulateRuntimeEvents.java` executes the original mailbox writer `0x400f45b4`
and reader `0x400f33d4` in all 288 combinations of independent type, previous,
current, notification and occupied/empty inputs. Complete four-field outputs and
pending flags match the rebuilt mailbox. Diagnostic formatting, the stack guard,
and single-thread memory ordering are explicit harness boundaries. This does not
qualify concurrent ISR or FreeRTOS behavior.

The portable composition check posts a notification through the real `CoreFlow`
mailbox, attempts a competing transition, runs `CoreFlow::tick`, and forwards the
accepted event to `HeadEvents`. The expected alarm and notification-screen effects
retain the notification identity. Both existing complete Core comparisons were
also replayed after the payload extension: 1,672 Core-flow scenarios and 1,662
command-dispatch scenarios match their preserved original execution records.
Their reports now bind the extended header; their golden records are unchanged.

## Reproduce

Run from this component with the documented hash-pinned analysis tools and an
existing verified Z119 project. Use a new output directory for original evidence.

```sh
python tests/run_runtime_events_emulation.py \
  --project <verified-project> --ghidra-home <ghidra> --java-home <jdk> \
  --output <new-output-directory>
python tools/validate_runtime_events.py \
  --original-directory <new-output-directory> --write-evidence
PYTHONPATH=tests python -m unittest test_runtime_events test_core_flow test_core_commands
```

The source adapters express semantic event fields, not the original C++ vtable
ABI. Application assembly must install the Runtime callback with its actual Core
instance and dispatch Head effects to concrete alarm and screen services. A
complete application image and physical validation remain separate gates.
