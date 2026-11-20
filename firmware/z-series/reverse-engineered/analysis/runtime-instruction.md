# Z119 program instruction format and argument validation

The original program instruction is two little-endian 32-bit words: an
opcode and its argument. The 58 opcode names come from the pointer table at
`0x3f42a3f4`, guarded by `0x400de7f1..0x400de7ff`. They are preserved in the
owned `OpCode` enum. `Instruction`, its argument predicate and temperature
decoder are in `include/zseries/runtime_instruction.hpp` and
`src/runtime_instruction.cpp`.

## Original behavior

Function `0x400dff64` rejects an opcode above 57 and implements these argument
rules. These are internal OEM instruction checks, not a safe external control
API or a substitute for the runtime's interlocks.

| Opcodes | Original accepted argument |
| --- | --- |
| `Pause` | 0..17 |
| `StepName`, `SubStepName`, `WaitDelay`, `WaitTime`, `SetTime`, `SetITMin`, `SetITMax`, `Setup` | Any 32-bit word |
| `ReachTarget`, `SetTarget` | Unsigned fixed point temperature 0..98 °C inclusive |
| `HoldFor`, `CircFor`, `DrainFor`, `FlagSet`, `FlagClear`, `WaitFor` | Nonzero |
| `MoveValve` | 1..6 |
| `SetPumps` | Original `((argument << 4) & 0xffffff8f) == 0` |
| `WaitUser` | 1..17 |
| `MspValve` | 1..9 |
| `MspPumps` | Exactly `0x40`, `0x50`, `0x60`, `0x70` |
| `ValveMode`, `Heater`, `RequireFilter` | 0 or 1 |
| `SetScreen`, `ShowScreen` | 0..102 |
| Remaining defined opcodes | Zero |

The `SetPumps` check deliberately preserves the original 32-bit shift:
argument bits 28..31 are discarded, bits 0..2 are permitted, and remaining
bits must be zero. Narrowing this to just arguments 0..7 would change OEM
behavior. A future external-input API can reject unsupported high bits
explicitly without rewriting the reconstruction's evidence.

Function `0x400dff0c` interprets the argument as unsigned fixed point with
eight fractional bits, multiplying by the original exact constant 1/256.
`ReachTarget`, `SetTarget`, `SetITMin`, and `SetITMax` use this argument kind;
the last two are not restricted by the OEM validity function to 0..98 °C.
The owned decoder rejects other opcodes without changing the caller's
output, replacing the original assertion boundary for invalid use.

The common-string reference guard at `0x400e0fb4` accepts `0x8000..0x8025`.
The embedded test at `0x400e2f4c` supplies the original helper names
`Instruction::isCommonStringRef()` and `Instruction::getCommonString()`.
The range helper is reconstructed; the string resolver and full dispatcher
are separate components.

## Verification

`tools/ghidra/EmulateRuntimeInstruction.java` executes both original
instruction bodies. It checks all defined opcodes and six out-of-range
opcodes against 463 argument values, covering small integers, bounds,
individual bits, high-nibble pump cases and deterministic additional words.
**29,632 original validation results** match the compiled owned predicate.
The four temperature-bearing opcodes also produce **1,852 independently
executed original decoder results**, all matching exactly.

The original validator's nested decoder call is modeled at the register
window boundary; the decoder body is executed independently for each
applicable input. Compiler binary64 conversion, multiplication and comparison
are explicit hooks. `ENTRY`/`RETW` window transitions are excluded. Neither
function contains an Xtensa hardware-loop instruction. This is scoped
instruction execution, not a complete emulated machine.

The committed CSV retains 1,792 original-execution cases and its SHA-256 is
checked by portable native tests. The full report pins source, generator,
instruction export, complete execution CSV and compiler details.

```sh
python3 tools/validate_runtime_instruction.py --prepare-input build/runtime-inputs.csv
JAVA_HOME=/path/to/jdk-25 /path/to/ghidra/support/analyzeHeadless \
  /path/to/z119-project firmware -process zseries_0_0_119.bin \
  -noanalysis -readOnly -scriptPath "$PWD/tools/ghidra" \
  -postScript EmulateRuntimeInstruction.java "$PWD/build/runtime-inputs.csv" \
  "$PWD/build/runtime-original.csv" -max-cpu 1
python3 tools/validate_runtime_instruction.py --original-csv build/runtime-original.csv \
  --export /path/to/z119-preserved-export --write-evidence
```

This module interprets and validates an instruction. It does not claim to
execute a brew program, schedule hardware, or establish physical validation.
