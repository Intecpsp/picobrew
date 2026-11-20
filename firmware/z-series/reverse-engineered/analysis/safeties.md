# Z119 timed safety monitors and filter input

`safeties.hpp` and `safeties.cpp` reconstruct the original `Safeties.h` body
at `0x400d9510` and its monitor helpers. The preserved 0.0.119 image is the
authority; addresses and instruction-export hashes are recorded in
`safeties-validation.json`. Original thermal limit and age checks execute in
the heater's slow timer, documented separately in `heater.md`.

## Object and operation evidence

| Original address / offset | Recovered meaning |
|---|---|
| Core `+0x4810` | Safeties controller |
| Controller `+4`, `+8`, `+0xc` | Shared flags, SensorsState, MSP state pointers |
| Controller `+0x10` | Enable flag, also used by heater thermal refresh |
| Controller `+0x18`, `+0x38`, `+0x58` | Hex-rise, Hex/Wort-delta, Wort/Drain-delta monitors |
| Monitor `+4`, `+8`, `+0x10`, `+0x18` | Enabled, unsigned deadline, double threshold, latched-flag pointer |
| Core `+0xc50` bytes 0–6 | Too hot, too cold, too old, Hex/Wort delta, Wort/Drain delta, Hex rise, filter missing |
| `0x400f7028` | Configures GPIO 36 as input and registers diagnostic commands |
| `0x400d7f7c` | Starts monitor and sets `now + delay` and threshold |
| `0x400d69c8` | Stops enabled monitor and clears its flag |
| `0x400d7fd8` | Latches a flag after its deadline when the threshold is exceeded |
| `0x400d9510` | Reads filter input, controls the monitors and updates their flags |

Core construction enables Safeties by default. Its main tick does nothing
while disabled. When enabled, it reads GPIO 36; exactly the value 1 means
filter missing. It snapshots the current thermal flags for diagnostics.

With MSP pump bit `0x10` clear, the first tick starts a Hex-rise monitor at
the current Hex temperature plus 20 °C, without a delay. It stops the other
two monitors. With that bit set, it stops the rise monitor and starts the
Hex/Wort monitor with a five-second delay and a 15 °C threshold. Valve
positions 4–9 additionally start the Wort/Drain monitor with a five-minute
delay and a 50 °C threshold. Moving the valve outside that range does **not**
stop an already active monitor; clearing the pump bit does.

The two delta measurements are absolute temperature differences. A negative
Hex rate supplies zero to the Hex/Wort monitor, suppressing a new trip while
cooling. A latched flag remains set. Starting a monitor does not clear an old
flag; stopping clears it only when that monitor was enabled. These original
details are deliberately preserved.

The original deadline condition is unsigned `now >= deadline`, and the
threshold test is strict `value > threshold`. Deadline addition wraps as
`uint32_t`; no elapsed-time rewrite has been applied. The fixtures cover
threshold equality, deadlines immediately before/at/after expiration,
addition rollover, disabled state, already-latched flags, cooling, pump
changes and valve changes.

## Runtime and platform integration

`core_control_state.cpp` transfers the recovered flags and thermal values
to the runtime input snapshot. The original application shares these through
CoreState; the portable types make that relationship explicit. Integration
tests run the actual monitor implementation followed by the accepted runtime
pause-priority logic. Monitor bypass, filter requirements and thermal pause
priority retain their original behavior. The binding does not invent a
runtime pause reason for stale data: the heater timer gates stale thermal
inputs separately.

The ESP32 adapter configures and reads the real GPIO through the pinned
Arduino SDK. Missing portable GPIO callbacks cause an explicit failure before
state changes. Those boundary errors are owned adapter behavior, distinct from
the original application's SDK and assertion paths.

## Validation and limits

The comparison executes the original Safeties tick and nested monitor bodies
through Ghidra's Xtensa instruction semantics. It adapts register windows at
`ENTRY`/`RETW` and explicitly supplies proven compiler arithmetic, GPIO,
clock and diagnostic boundaries. Unknown calls and hardware loop instructions
are rejected. It is not full CPU, RTOS, interrupt or hardware emulation.

All **660 scenarios / 1,435 ticks** match the native source. All **10,045
binary64 fields** are bit-identical in the recorded environment; monitor
enable/deadline/latch values, thermal snapshots and GPIO-read counts match
exactly. The ordinary test suite retains 292 representative original cases
and validates their input/source provenance. No machine was connected or
actuated by this workflow.

```sh
python3 tools/validate_safeties.py --prepare-input build/safeties-input.json
# Run EmulateSafeties.java with -noanalysis -readOnly in the preserved project;
# arguments: absolute input JSON path, absolute original-output JSONL path.
python3 tools/validate_safeties.py --original-jsonl build/safeties-original.jsonl \
  --export <preserved-export> --write-evidence
python3 -m unittest discover -s tests -p test_safeties.py
```
