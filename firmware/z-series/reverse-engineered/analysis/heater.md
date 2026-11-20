# Z119 heater, PID, and timer reconstruction

`include/zseries/heater.hpp` and `src/heater.cpp` recover the heater state
changes, PID calculation, and original timer callbacks. The hardware boundary
is an explicit GPIO callback; this is not yet an installed ESP32 application.
The source belongs to the OEM reconstruction. It preserves the original
behavior, including distinctions a modified firmware may later change.

## Original addresses and object binding

All addresses refer to preserved `zseries_0_0_119.bin`, SHA-256
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.

| Original address | Recovered operation |
|---|---|
| `0x400d001c` | PID constructor; sample interval starts at 100 ms |
| `0x400d0050` | Initialize previous input, error, P and D; retain I |
| `0x400d0094` | Set gains, scale I/D by sample interval, apply direction |
| `0x400d0180` | Set output limits and clamp active output/I |
| `0x400d0240` | Change PID mode; initialize on either transition |
| `0x400d0278` | Change PID direction |
| `0x400d02cc` | Bind input/output/target pointers and configure PID |
| `0x40080414` | PID computation |
| `0x400d1a40` | Set enable flag and GPIO 19 |
| `0x400d1a74` | Stop regulation, lower output pin, set manual mode |
| `0x400d6d70` | Start regulation and set automatic mode |
| `0x400d6e7c` | Set target, capped at estimated boiling point minus 2 °C |
| `0x400de27c` | Select Big/Small boiler gains |
| `0x400fc3e4` | Configure output/enable pins and initialize both low |
| `0x40080540` | 20 ms heater countdown callback |
| `0x400805dc` | 1 s callback, performing heater work every other invocation |

The expanded constructor at `0x40120a04` binds Heater at Core `+0x4940`,
its PID at Heater `+0x40`, HeaterState at Core `+0x9d8`, SensorsState at Core
`+0xc58`, and the three thermal flag bytes at Core `+0xc50`.
The constructor assigns output GPIO **12**. The separate enable method
contains GPIO **19** directly. This is application evidence for these pins;
it does not identify a particular Z1/Z2 board revision.

PID setup in `0x40113078` binds the Wort input, target, and output and sets
output limits to 0–100. Big boiler gains are P=34, I=13, D=3; Small gains are
P=30, I=10, D=0. The PID sample interval remains **100 ms**, although actual
computation occurs every other 1 s callback. The reconstruction keeps both
observed values. The boiler names are not a proven Z1/Z2 mapping.

## Behavior retained from the original

The PID calculates error, proportional term, clamped accumulated integral,
and derivative on measured input. It clamps `P + I - D` to the output range.
Changing either direction while active negates the scaled gains. Changing
either mode initializes the previous input and error/P/D, while preserving
the accumulated integral. Leaving automatic mode sets output to its minimum.

Enabling the heater and starting regulation are separate operations. The
slow callback gates output on the over-temperature and stale-temperature
flags. It does **not** inspect the enable flag or the under-temperature flag
at this particular gate. Other application control paths are separate.

The 20 ms callback decrements a positive counter and returns. It writes LOW
when the counter is already zero at callback entry. Consequently a counter
of one reaches zero on one call and switches LOW on the following call.
Negative counters do nothing. The exact pin event sequence is covered by
the original-code fixtures.

The slow callback recomputes the thermal flags only when checking is enabled.
Otherwise existing flags remain in force. It always updates the smoothed
Wort/Hex rates on its active phase, even when heater regulation is stopped.
The rate calculation is `((current - previous) * 0.02) * 30 + rate * 0.98`,
with the original operation order retained. Over-temperature or stale input
sets output and displayed integral to zero and lowers the pin, while leaving
the internal PID integral and countdown unchanged.

Initialized image data gives these strict comparison limits:

| Sensor | Minimum °C | Maximum °C | Maximum age |
|---|---:|---:|---:|
| Hex | 0 | 110 | 4,000 ms |
| Wort | 0 | 105 | 4,000 ms |
| Drain | 0 | 105 | `UINT32_MAX` |
| Ambient | -20 | 62 | `UINT32_MAX` |

The four ages are at `0x3ffbdc48`; min/max double pairs are at `0x3ffbdc58`.
An update time of zero means age `UINT32_MAX`; otherwise age is unsigned
`now - updated`, preserving timer rollover. Equality at a limit is accepted.

The target cap uses SensorsState `+0x278`. The pressure update at `0x400d91ac`
sets this estimated boiling temperature to
`(pressure_Pa - 101325) * 0.00034724743 + 100`.
When a requested target exceeds the cap, the excess is retained separately.

## Verification and limits

`heater-validation.json` records **583 scenarios / 930 original actions**.
All state words and GPIO events match, and all **24,180 binary64 field
comparisons are bit-identical** in the recorded environment. The portable
fixture retains 215 scenarios, including a full output window, mode and
direction changes, rejected negative gains, clipping, stale timestamps,
rollover, threshold equality, inactive phases, and each thermal flag.

`tools/ghidra/EmulateHeater.java` executes the original heater, PID, and timer
instructions from the preserved analyzed image. It adapts Xtensa register
windows at function entry/return and executes the relevant nested application
functions. Arithmetic library functions, GPIO, milliseconds, diagnostic
logging, and diagnostic command registration are explicit modeled boundaries.
It fails on an unknown call, an assertion, or an unmodeled hardware loop.
This is not a complete CPU, interrupt, RTOS, electrical, or brewing simulation.

The owned typed API rejects missing GPIO callbacks before changing state.
It additionally rejects nonfinite gains, limits, or target requests, invalid
boiler values, and invalid sample intervals. Those explicit adapter guards
are tested separately; they are not attributed to original instructions.
The compute/timer API requires finite state and an output representable as
`int32_t`, as provided by the recovered 0–100 setup. Default initialization
of owned records is not a claim that every original C++ member was initialized.

Reproduction uses the preserved Ghidra project with the six real ESP32
segments loaded, `-noanalysis -readOnly`, and the local script directory:

```sh
python3 tools/validate_heater.py --prepare-input build/heater-input.json
# analyzeHeadless <project-directory> firmware -process zseries_0_0_119.bin \
#   -noanalysis -readOnly -scriptPath <component>/tools/ghidra \
#   -postScript EmulateHeater.java <absolute-input-json> <absolute-output-jsonl>
python3 tools/validate_heater.py --original-jsonl build/heater-original.jsonl \
  --export <preserved-export> --heater-export build/heater-entry-export \
  --write-evidence
python3 -m unittest discover -s tests -p test_heater.py
```

The focused heater entry export is produced by `ExportZHeaterEntries.java`;
the source and instruction hashes needed to identify the run are retained
in the validation report. Ordinary fixture tests need no Ghidra installation.
