# Z119 Core orchestration and state flow

`core_flow.hpp` / `core_flow.cpp` express the original Core state transitions,
main-loop ordering, event priority, Wi-Fi reactions and session gates as owned
C++11 types. The source has no original RAM/vtable dependency. The peripheral,
network, UI and persistence calls use required typed interfaces; implementations
must be supplied, and missing services do not become successful no-ops.

## Address traceability

| Original body | Reconstructed behavior |
|---|---|
| `0x4011cbf4` | Core transition, event emission, date and per-state effects |
| `0x4011ce5c` | Wi-Fi event handling, persistence and Head/PicoStill forwarding |
| `0x40120120` | Component tick order, pending-event priority and no-event state loop |
| `0x400f4bf4` | Deferred Runtime start, program-ID guard and rinse requirement |
| `0x4011d1e4` | Online/offline session selection while Starting |
| `0x400e69f4` | Unsigned elapsed-time test and backwards-clock diagnostic |
| `0x400edaec` | Azure enable/recovery decision from EEPROM |
| `0x400edbb0` | Configuration save with heater timer disable/enable |
| `0x400ee3b4` | First sensor-startup error remains latched |
| `0x401bf3d4`, `0x401bf4e8`, `0x400d1184` | Empty-string, MSP-error and update-availability predicates |
| `0x400f4424`, `0x400f33d4`, `0x400f077c`, `0x400f41ec` | Original event consumption |
| `0x401bf484` | Core event payload reset |

The CoreState object begins at Core `+0x730`: state ID at `+0x734`, state date
at `+0x738`, service mode at `+0x73c`, pending rinse at `+0x73d`, offline use
at `+0x73e`, and session fields starting at `+0x740`. Service mode is confirmed
by the original `SERVICE MODE ON` diagnostic. The deferred-runtime flag is
Core `+0x3cf9`. Boot delay is 1100 ms, Ethernet timeout 10,000 ms, and standby
delay 1,800,000 ms, from the original Core constructor.

## Loop and mailbox behavior

Each pass services MSP, temperatures (when enabled), accelerometer, pressure,
PicoStill, System, Safeties, power button, firmware update, binary download,
serial commands, Wi-Fi, local HTTP, Z backend, encoder, Head, Runtime, HTTP
backend, serial logger, registration, program downloader, session logger and
Azure, in that order.

It then consumes at most one pending event, in this priority:

1. Wi-Fi event.
2. Runtime event.
3. Core command.
4. Power-button event.
5. Text command.
6. PicoStill event.

Only a pass with no event advances the normal Core state loop. A power-button
event enqueues a Core command for a later pass. Runtime transitions move
Idle/Starting to Running, or Running to Stopping; the latter marks session stop,
notifies Azure and then forwards the Runtime event to Head. PicoStill's Starting
event can begin an online session after Head notification.

The original `EventQ` is a single pending slot. Its put bodies, including
`0x400f02e4` and `0x400f4900`, preserve an already pending event and drop/log a
new one. `PendingEvent<T>` retains that rule. It is separate from FreeRTOS's
task and timer facilities. The Core fixture records **calls** to the Head
event endpoint; final delivery/drop behavior belongs to the Head mailbox.

## State and persistence details

Transitions emit the Core event before updating state and date. A redundant
transition has no state or component effect. Entering Wi-Fi connection states
clears the active network type, checks pending SSID and requests connection.
Successful newly requested credentials disable heater timers around EEPROM
write, then re-enable them. Ethernet's successful save has its original
different call order, without adding that Wi-Fi wrapper.

Boot requires no System/MSP error and nonzero timestamps for all four thermal
channels. Missing channels latch the first error, checking Wort before Hex,
Drain and Ambient. Service mode can bypass network setup after these checks.
Ordinary boot selects Ethernet, saved Wi-Fi, or NeedsNetwork. The original
relative-time arithmetic, including its backwards-clock diagnostic followed
by unsigned subtraction, is preserved.

Online normal sessions wait for the session logger's ready state; offline
sessions and PicoStill have their original gates. The pending rinse set is
session types 6–9 and 12–14, recovered from the set initializer `0x4010e01c`.
A required rinse starts program 1 after stopping. The firmware's numeric
state 17 is an enum-count diagnostic path, not an invented extra state.

## Validation boundary

All **1,672 original scenarios** match native source across **17 executed
original bodies** and **17,442 ordered service-call events**. State, mailbox
pending bits, assertion lines and credential contents match exactly. Cases
cover all transition pairs in 0–18, Wi-Fi events, service mode, session modes,
network failure paths, temperature startup faults, time boundaries and every
combination of the six pending-event slots. The portable suite retains 368
original cases and a two-pass button-to-command integration check.

The harness executes the original Core bodies with explicit component, clock,
bounded-string and set-lookup boundaries. The orchestration ADC fixture uses
no valid channel; full acquisition and thermal calculations have their own
accepted original-execution comparisons. These counts therefore do not claim
nested execution of every peripheral or network subsystem. The Head renderer,
backend request payloads and final application assembly remain separate integrations.
The dispatcher and program-start metadata are now independently checked in
[Core commands](core-commands.md).

The evidence report pins input, executed instruction exports, harness, source
and portable fixtures. No machine was accessed, and this module alone does
not constitute a linked or installable application.

```sh
python3 tools/validate_core_flow.py --prepare-input build/core-flow-input.json
# Run EmulateCoreFlow.java with -noanalysis -readOnly in the preserved project;
# arguments: absolute input JSON and original-output JSONL paths.
python3 tools/validate_core_flow.py --original-jsonl build/core-flow-original.jsonl \
  --export <preserved-export> --write-evidence
python3 -m unittest discover -s tests -p test_core_flow.py
```
