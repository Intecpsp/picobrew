# Z119 program runtime and quantity reconstruction

## Accepted implementation

`src/program_runtime.cpp`, `src/program_runtime_execution.cpp`, and their header
contain the recovered interpreter, rather than generated decompiler C or an
opcode inventory. `src/quantity.cpp` supplies its recovered unit conversion.
`src/program_runtime_heater.cpp` connects the interpreter to the independently
reconstructed heater controller; pump and valve commands call `msp_protocol.c`.

The source passes **5,792 original-instruction cases from 52 original function
bodies**. Each case compares the represented runtime state, expansion stack,
step-name buffers, returned value, assertion boundary, and ordered service calls.
The focused test also links the real runtime, heater, and MSP implementations to
check their integration. All four new C++ modules compile with the pinned official
Xtensa GCC 5.2 compiler using warnings as errors.

This closes the interpreter dependency of a Z application. It does not establish
a linked ESP32 application, a byte-identical OEM rebuild, installed firmware, or
physical brewing validation. Repository lookup, queue storage, PicoStill transport,
and platform services must still be supplied by the enclosing application. Missing
services produce explicit errors; the production implementation does not provide
default no-op hardware callbacks.

## Original evidence and source boundaries

The input is the preserved `oem/upstream/zseries_0_0_119.bin`, SHA-256
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.
`program-runtime-validation.json` pins the input, fixture, generators, analysis
tools, bounded execution settings, and validated source hashes. The fixture
records exact byte ranges and hashes for every executed original function.

| Original entry | Reconstructed behavior |
| --- | --- |
| `0x4010a48c` | All 58 opcode dispatch cases, bookkeeping, and expansion sequences |
| `0x4010b200` | Periodic command processing, state decisions, next instruction, stop sequence, and notifications |
| `0x400e0374`, `0x400e0464`, `0x400e06fc` | Raw push, validated push, and pop of the 256-entry LIFO stack |
| `0x400e01d8`, `0x400e02f4`, `0x400e0348`, `0x400e04a0` | Program validation, instruction indexing, and reverse loading |
| `0x400e05c8` | Program selection and program-specific runtime flags |
| `0x400d12b0` | Instruction duration with original unsigned multiplication |
| `0x400e6bf8`, `0x401bf29c` | Wait completion and eligible skip operations |
| `0x400f46e8`, `0x400ef038` | Runtime state transitions and reset |
| `0x400e6240`, `0x400e62d8` | Save remaining work and restore the paused execution context |
| `0x400e6364`, `0x400e6428`, `0x400e64d0`, `0x400e660c` | PicoStill pauses, reference/boiling steps, and state progression |
| `0x400d20ac`, `0x400e6124` | Pause-reason priority and cooling-fan decisions |
| `0x400ec360`, `0x400ec5fc` | Queued-work and total remaining-time estimates |
| `0x400d742c`, `0x400daf04` | T1-drop watch and disable operation |
| `0x400e03a8` | Setup screen expansion |
| `0x400e0fb4`, `0x400e0fe4`, `0x3f42a1c0` | Common and per-program string lookup; 38 common strings |
| `0x401bf40c`, `0x400dee30`, `0x400def90` | Quantity compatibility, conversion, and Fahrenheit wrapper |

The instruction validator and fixed-point temperature decoder are reused from
`runtime_instruction.cpp`; the runtime does not duplicate their rules. The unit
and opcode enums come from the recovered original DROM tables in `enums.hpp`.
Decompiler output served as navigation evidence. The acceptance fixtures execute
the original instruction bodies, including original branch decisions and memory
writes, independently of the reconstructed C++ implementation.

## Interpreter behavior

The stack stores instructions in reverse execution order. A high-level operation
such as ReachTarget, MoveValve, or RinseCycle appends smaller instructions. Each
periodic call consumes one command and dispatches at most one new instruction.
The next periodic call evaluates that instruction's completion condition.

Program validation requires a nonempty name, a nonempty instruction array of at
most 256 entries, a consistent string table, valid instruction arguments, and
valid references for step/substep names. `ProgramView` borrows its arrays and
strings; their storage must remain alive while the program runs. Program loading
checks capacity before appending any instructions.

The two original valve lookup tables remain unassigned to a physical Z1/Z2 name:

| Logical index | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Table `0x3f425568` | 3 | 2 | 4 | 5 | 6 | 8 | 9 |
| Table `0x3f42556f` | 3 | 2 | 4 | 6 | 7 | 8 | 9 |

MoveValve expansion preserves the observed distinction between requested and
actual keg-pump state, and stops/restarts PID as required by that sequence. Pause
stops the heater's PID hardware and clears the active pump outputs, while retaining
the runtime's separate PID-active flag so its resume expansion can restore PID.

The recovered periodic loop handles Start, Stop, Pause, Resume, Skip, Drain,
setup instructions, pre-brew tests, temperature/duration/location edits, and manual
steps. Commands received during an unfinished physical valve move are requeued
when the original does so. Safety reasons retain their original priority:
system, MSP, too hot, too cold, monitored deltas/rise, required filter, PicoStill,
then a user pause command.

Core constructor `0x40120a04` confirms that these offsets are relative to the
Runtime object's CoreState pointer at `+0x110`. The complete Core stores CoreState
at `+0x730`, RuntimeState at `+0xa20`, and Runtime at `+0x12bd0`. The T1-drop watch
state is RuntimeState `+0x1d8`; its input points to CoreState `+0x958`. It tracks a
maximum and triggers when the drop exceeds its threshold. The resulting runtime
notification is the original `T1Dropped` enum value. Other PicoStill signal
records with unresolved physical interpretation retain their original offsets in
the typed observation structure.

## Preserved OEM details

These behaviors are deliberate fidelity choices in the reconstruction, not new
product policies:

- Timed wait completion is strict: `start + duration < now`, using 32-bit unsigned
  arithmetic. Separate original assertions reject a clock earlier than the
  instruction start. Duration multiplication can wrap.
- HoldFor, CircFor, and DrainFor complete their high-level instruction immediately
  after expansion; the generated WaitFor performs the actual wait.
- Saving a partially completed WaitFor subtracts whole elapsed seconds. Saving a
  fully elapsed wait hits the original assertion boundary.
- A manual step first converts its temperature to Fahrenheit, truncates to integer
  Fahrenheit, then converts to fixed-point Celsius. It does not simply retain the
  original fractional Celsius value.
- A queued WaitDelay uses the same estimated time field as WaitTime in the original
  estimator. Unknown estimates also participate in ordinary unsigned addition.
- Runtime reset preserves StillState's paused ID, which the original reset does
  not write. It also preserves the T1-watch maximum, threshold, and triggered flag;
  the preceding reset call disables the watch.
- Program selection reports a found program even if its subsequent stack load is
  rejected. The typed runtime preserves that decision and exposes
  `program_load_result` separately.
- The common manual-step label is exactly `Step %d`, including signed formatting.
- Equal unknown unit enum values are compatible in the OEM quantity function.
  Unequal unknown values fail compatibility.

Portable result codes replace fatal assertions and missing callback failures.
Out-of-range manual numeric conversions and nonfinite current-temperature delta
estimates are explicit `InvalidNumericDomain` errors rather than undefined host
C++ conversions. Those added API boundaries are separate from the finite original
instruction cases. The original interpreter does not validate each instruction
again at dispatch; program loading remains the validation boundary. A portable
array bound additionally rejects out-of-range direct MoveValve indices.

## Quantity constants

| Conversion | Original calculation |
| --- | --- |
| Liter to Gallon | value / 3.78541 |
| Gallon to Liter | value × 3.78541 |
| Gram to Ounce | value / 28.349523125 |
| Ounce to Gram | value × 28.349523125 |
| Celsius to Fahrenheit | value × 1.8 + 32 |
| Fahrenheit to Celsius | (value − 32) / 1.8 |

These are the original constants, including their precision. Incompatible units
return false in the typed API in place of the original fatal assertion. Its result
value is zero on that path, matching the OEM's fallback after a returning assertion.

## Validation and limits

The deterministic cases include every opcode; both valve maps; pump/PID expansion
combinations; stack limits; wrapped durations and timing boundaries; all relevant
command/wait combinations; threshold-adjacent temperatures and PicoStill signals;
pause/resume, stop, and state transitions; all 2,048 selected safety-priority
combinations; manual-step conversions; 1,089 quantity conversions and 121 unit
compatibility cases; repository-selection flags; program validation; and full
periodic runtime ticks.

The original execution harness uses the verified read-only Ghidra project and a
200,000-instruction budget per case. It normalizes reviewed Xtensa register-window
ENTRY/CALL8/RETW boundaries. MEMW is treated as sequential memory ordering in this
single-thread fixture; interrupt and concurrent-memory behavior are not emulated.
An unexpected hardware LOOP is an error rather than silently omitted execution.

Explicit boundaries supply pinned SDK scalar arithmetic, diagnostic formatting,
ROM string/memory operations, a clock, repository lookup, queues, heater/PicoStill
services, and the independently instruction-validated MSP operations. The known
`Step %d` format is the only variadic formatting supplied by the fixture. Those
boundaries are listed and counted in the fixture. No test opens a device, changes
GPIO, or sends an I2C transaction to physical hardware.

The additional native integration case uses the actual recovered heater and MSP
modules. It verifies boiling-point target limiting, shared target observation,
PID start/stop, retained runtime PID intent during pause, pump command framing,
ordered GPIO callbacks, thermal snapshot refresh, and propagation of a heater
service failure.

Run the committed acceptance checks without Ghidra:

```sh
make -C firmware/z-series/reverse-engineered verify
```

Regenerate original instruction evidence using the pinned tools and a verified
preserved project:

```sh
python3 firmware/z-series/reverse-engineered/tests/run_program_runtime_emulation.py \
  --project /path/to/z119-project \
  --ghidra-home /path/to/ghidra_12.2_PUBLIC \
  --java-home /path/to/jdk-25.0.5+8 \
  --output /path/to/new-runtime-evidence
```

The runner requires a new output directory, verifies the original archive/tool
pins and every mapped image segment, validates executed-function byte hashes, and
rejects incomplete outputs even if headless Ghidra itself exits successfully.
