# Z119 recipe parsing and program generation

This component recovers the application logic in `ProgramParser.h` and the two
HTTP recipe builders. It uses the real, separately downloaded ArduinoJson
dependency; no JSON parser or SDK implementation is copied into the project.
ArduinoJson **6.21.6 is an explicitly selected rebuild dependency**. The OEM
minor version has not been established.

## Original boundaries and readable source

| Original address | Recovered role | Readable implementation |
| --- | --- | --- |
| `0x40116968` | Local JSON parser, program generation, metadata and completion order | `program_recipe.cpp`, `program_recipe_json.cpp` |
| `0x40114f8c` | Zymatic/Z-recipe program builder | `build_http_recipe`, Zymatic format |
| `0x4011595c` | PicoPak builder, ABV/IBU timing interpolation | `build_http_recipe`, PicoPak format |
| `0x40114344` | Detect a recipe temperature above estimated boil point minus 2 C | HTTP setup flag scan |
| `0x400d165c` | Per-valve integer Fahrenheit cap and round trip | `capped_fahrenheit` |
| `0x401158bc` | Strict integer MinTime/MaxTime, default -1 | HTTP JSON step adapter |
| `0x40114068` | Mutable-input deserialization and nesting bound | Selected library plus explicit compatibility adapter |
| `0x401bf724` | Allocate a 16-byte JSON member/element slot | Capacity evidence for the selected library |
| `0x401142bc` | Numeric conversion, including negative integer strings | `json_adapter::number` |

`ProgramBuffer`, the shared duration estimator, `Quantity`, and the runtime are
the previously recovered implementations. Recipe generation calls them
directly. Generated names and instructions are owned by `ProgramBuffer`; the
mutable JSON input can be discarded after the parser returns.

## Recipe formats

| Behavior | Local JSON | Zymatic / Z-recipe HTTP | PicoPak HTTP |
| --- | --- | --- | --- |
| Session type / ID | LocalBrew / 0 | Passed by backend | Passed by backend |
| Temperature field | Celsius double | Fahrenheit integer | Fahrenheit integer |
| Time / Drain fields | Seconds | Minutes | Minutes |
| Valve locations | 1..6; other values become 1 | 0..5 map to 1..6; pause location 6 and invalid values use valve 1 for the temperature cap | 1..6; invalid active steps emit no valve/heat/hold/drain instructions |
| Pause | StepType 2 | Location 6 | Action 1 |
| Other action handling | Other StepTypes use normal steps | No Action field | Action 0 runs a step; other actions keep only its name |
| Initial water | 11.36 L | Numeric StartWater, otherwise 11.36 L | 6.81 L |
| Timing adjustment | Temperature excess if Time > 600 | Temperature excess if Time > 10 | ABV/IBU interpolation, then temperature excess if adjusted Time > 10 |
| Metadata parameters | Defaults, with water set | Defaults, with water set | Supplied parameters copied, water replaced |

All formats emit the original Setup and CompleteBrew instructions. Setup flags
preserve beer/PicoPak mode, resume mode, and the pre-cap over-temperature scan.
PicoPak timing uses single-precision division and multiplication followed by
integer truncation. The conditions for applying ABV and IBU adjustments, the
0..10 user range, and fallback to recipe targets are retained.

The original valve-1 cap is 94.45 C. Local recipes retain that Celsius value;
HTTP recipes pass through the original integer-Fahrenheit conversion. All
formats also cap at estimated boil temperature minus 2 C and may add
`truncate((requested_C - maximum_C) * 162)` seconds. The recovered code retains
the original reversed conversion `(maximum_C - 32) / 1.8` used after this cap
to decide whether to emit ReachTarget. This is documented behavior, not a new
temperature policy.

## JSON dependency and compatibility

`dependencies/arduinojson.json` pins the official tag object, commit, archive
size/hash, and all 135 source files through a deterministic tree hash.
`tools/acquire_arduinojson.py` verifies the archive and extracted source and
stores the dependency in the ignored build directory. The adapter uses double
precision, 32-bit target integer types, mutable-input zero-copy decoding,
comments, and a nesting limit of 10.

The original local pool has 1,024 bytes, and HTTP pools have 4,096 bytes. In the
original ABI these hold 64 and 256 sixteen-byte slots respectively. The native
64-bit build uses the same **slot counts**; a separately executed 32-bit build
checks the exact original byte capacities and integer width. Compile-time
assertions require 1,024/4,096 bytes on a 32-bit target.

The compatibility adapter preserves legacy escaped-character handling and
rejects active Unicode escapes as NotSupported, including when an earlier
syntax, capacity or depth error has priority. It also preserves the OEM
unsigned-conversion quirk for negative integer strings. These narrow adapters
surround the selected library; they do not implement a second JSON parser.

The selected library differs on malformed literal tokens such as `trux`,
`falsx`, `nulx`, and `truex`. It also retains a partial scalar for `1x` after
returning InvalidInput. Exact observations are recorded in
`program-recipe-compatibility.json`. These differences are explicit exceptions
to parser equivalence. The program-builder acceptance requires exact agreement
for every application case. A separate 64-bit host observation for the number
4294967296 is absent in the 32-bit replay, which reproduces the OEM overflow
path.

## Original-instruction validation

The accepted fixture contains **592 cases across 70 executed original function
bodies**. All **510 application cases** agree exactly on generated names,
instructions, metadata, assertion results and observed completion state. The
remaining 82 cases exercise JSON values, errors, nesting, comments, escapes and
memory limits; five malformed-token diagnostic differences are explicitly
recorded. The four reviewed TRUNC.S sites execute 67 times in this corpus.

The same 592 cases pass the separate 32-bit replay. Four focused native test
groups pass, including actual Runtime loading, input-string lifetime, the
distinct local/HTTP failure behavior, and complete source/input provenance.
Both production objects compile with pinned Xtensa GCC 5.2.

`tests/run_program_recipe_emulation.py` verifies all mapped original image
bytes before running a read-only Ghidra project. It executes the real original
JSON parser, builders, ProgramBuffer methods and shared metadata/duration
functions. Every executed function's original ranges and hashes are retained
in `tests/fixtures/program-recipe-original.json`; the runner verifies those
ranges against the preserved input file before accepting the fixture.

The executor adapts reviewed ENTRY/CALL8/RETW register windows and sequential
MEMW. It exposes explicit SDK scalar math, ROM memory/string functions,
single-thread static-initializer guards, diagnostics, error-text output and
completion notification boundaries. It neither runs an RTOS nor touches
physical hardware. Unknown calls and hardware-loop instructions fail the run.
Periodic progress files bind the image, input cases and executor hashes and can
resume an interrupted run without repeating completed cases.

### Narrow TRUNC.S correction

Ghidra 12.2's Xtensa definition computes its final TRUNC.S conversion with an
integer-bit scale rather than the float scale computed on the previous line.
For immediate zero, this incorrectly multiplies by the float bit pattern 1.
The decompiler consequently shows an implausible `1.4013e-45` multiplier in
PicoPak's interpolation branches.

The executor handles **only** original sites `0x4011602e`, `0x40116064`,
`0x401160ba`, and `0x401160f0`, after verifying their bytes are `00 40 9a`
(`trunc.s a4, f0, 0`). It requires finite input in signed-32 range and implements
rounding toward zero with scale 1.0. Every use is counted in the fixture.
Other TRUNC.S sites fail rather than silently using this boundary. No installed
Ghidra file is modified. Floating-point status flags and nonfinite/overflow
cases are outside this boundary's claim.

The instruction behavior is specified by Cadence's [Xtensa ISA Summary,
TRUNC.S, section 8.3.327, page 632](https://www.cadence.com/content/dam/cadence-www/global/en_US/documents/tools/silicon-solutions/compute-ip/isa-summary.pdf#page=633).

## Reproduce

```sh
python3 tools/acquire_arduinojson.py
python3 -m unittest discover -s tests -p 'test_program_recipe.py' -v

python3 tests/run_program_recipe_emulation.py \
  --project /path/to/verified/z119-project \
  --ghidra-home /path/to/ghidra_12.2_PUBLIC \
  --java-home /path/to/jdk-25.0.5+8 \
  --output /new/output/directory --timeout 1800

python3 tests/run_program_recipe_32bit.py \
  --toolchain /path/to/optional/i386-test-tools \
  --fixture tests/fixtures/program-recipe-original.json \
  --output /new/32bit-validation.json
```

The optional 32-bit runner downloads hash-pinned Debian multilib packages and
qemu-i386 without changing the system installation. It requires GCC 14, records
the actual compiler driver/front-end hashes, verifies the resulting 32-bit ABI,
and replays every original fixture. The ordinary native suite needs only the
selected ArduinoJson dependency and the existing C/C++ toolchain.

`program-recipe-target-build.json` records compilation of both production
objects with the pinned Xtensa GCC 5.2 toolchain. This is object-level evidence.
The completed Z firmware still requires the surrounding backend request/state
bindings and the remaining application modules before a linked image can be
claimed. The local completion callback represents the original state-7 update
and notification; HTTP request state, response IDs/Guid and session envelopes
remain the backend's responsibility. Failed HTTP decoding preserves an existing
program, while failed local decoding clears it, matching the original order.
