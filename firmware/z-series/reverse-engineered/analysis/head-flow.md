# Z119 front-panel flow

`head_flow.hpp/.cpp` recovers the outer front-panel controller: event priority,
timer behavior, command posting, alarm timeout and selection of the next screen.
Event-specific handlers and screen presentation are required services with no
successful default implementation. This module is a verified portion of Head,
not a claim that the complete front panel has already been linked.

| Original entry | Recovered operation |
| --- | --- |
| `0x40109958` | Process one pending event, timers, ScreenLibrary and Display |
| `0x401070e8` | Select the screen appropriate to the current Core state |
| `0x401074f8` | Post a Core command and apply the original screen-refresh rules |
| `0x400deb58` | Set the System alarm and start or clear its timeout |
| `0x400deb2c` | Start a nonzero-duration timer |
| `0x400d1cbc` | Reset the deadline of an already active timer |

## Event and timer behavior

One event is selected per tick in this order: Core, button, encoder, text input,
temperature, time, ABV, valve, quantity, PicoStill, Wi-Fi, Runtime, UI command,
Azure. Download progress is refreshed only when none of these events is pending.
Processing the PicoStill event refreshes its screen only when Runtime is Running
and its current instruction is `WaitStillReady`.

Both timer deadlines use the OEM unsigned `deadline <= now` test. Expiring the
transient timer first clears its active flag/deadline and synchronizes the UI.
Expiring the alarm timer turns the System alarm off and posts **Core Stop (26)**.
It does not select a numbered screen. The original diagnostic samples the clock
before this action. A fresh clock sample is then passed to both ScreenLibrary
and Display; their ticks run after event and timer processing.

Command posting preserves the screen-suppression mask and the special starting
message for commands 14–16. Posting to an occupied Core mailbox retains the
original drop-new behavior; the concrete adapter must distinguish “queue
operation serviced” from “new message accepted.”

The constructor durations are 7,000 ms, 30,000 ms and 7,200,000 ms. Download
percentage retains the original signed division after a wrapped 32-bit
multiplication by 100; an absent or nonpositive total yields zero. Arithmetic
and timer rollover behavior are recorded rather than silently redesigned.

## State mapping

The original Head object is Core + `0x5e00`. Its Display is Head + `0x17f8` and
ScreenLibrary is Head + `0x38a8`. CoreState + `0x2f4` is RuntimeStateId,
`0x2fc` is pause reason, `0x36c` is the current instruction opcode, and
`0x380` is the Runtime screen. These offsets agree with the independently
validated ProgramRuntime layout.

## Verification and limits

`tools/ghidra/EmulateHeadFlow.java` executes the six original bodies and records
ordered handler/presentation/command/alarm calls, timer fields, pending-event
bits and every clock read. `tools/validate_head_flow.py` compares these results
with the typed source for all Core states, all defined commands, every event
pair and additional deterministic pending masks, deadline boundaries, rollover,
download arithmetic and alarm-triggered Stop. The retained originals and exact
source hashes are recorded in `analysis/head-flow-validation.json` and replayed
by `tests/test_head_flow.py`.

Event-handler bodies, translated text, presentation, System alarm hardware,
CoreCommand default construction and mailbox delivery are explicit execution
boundaries. Their required implementations are not replaced by firmware stubs.
The fixture initializes the original stack guard deterministically and fails
on a modified guard, unknown call or unexpected assertion. No physical device,
complete application image or concurrent hardware behavior is claimed.
