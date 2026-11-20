# Original Z119 program repository and writer

The resolver at `0x400d7df8` is a small local repository, distinct from the
HTTP backend. Core constructor `0x40120a04` stores a pointer to ProgramBuffer
at Core + `0x17b8` in the resolver at Core + `0x3cfc`. Runtime + `0xfc` points
to that resolver; Core start handler `0x4011d010` independently confirms the
same address. ZBackend at Core + `0xfff0` supplies recipe/server operations.

## Recovered source

`src/program_catalog.cpp` contains all **23 built-in programs**, expressed as
685 typed `Instruction` entries and 52 local strings. Every instruction has its
original address in a comment. The program records retain their original session
type, unique ID, name, instruction order, string table and counts. All programs
also use the separately reconstructed common-string table when referenced.

`tools/recover_programs.py` reads the preserved image, verifies its identity,
decodes the reviewed resolver mapping, and regenerates both the catalog and
`analysis/programs.json`. The latter records original addresses and hashes for
each record, instruction array, string table and terminated string. The normal
test suite verifies that generated source still matches these original bytes.
The rebuilt application uses ordinary owned C++ data and does not read the OEM
binary, absolute addresses or decompiler output at runtime.

| Program IDs | Resolver behavior |
|---|---|
| 1–19, 25–28 | Return the corresponding static program |
| 20–24 | Return the same shared buffered program, without validating it |
| 0, 29, other integers | Return null |

`ProgramRepository::runtime_find` supplies the existing runtime service signature.
The application can use the repository as its context or delegate to it from a
composite application context. The original runtime reads the heating estimate
from the shared buffer even when selecting a static program; the adapter retains
that behavior. A found but invalid buffered program is still considered selected
by the original runtime, while its rejected stack load is exposed separately.

## Buffer and writer behavior

`ProgramBuffer` owns 256 instructions, 64 string slots of 48 bytes each, and a
stable pointer table. It is noncopyable so returned views cannot accidentally
point into another object's storage. Reset clears counts, sets the session type
to Unknown, writes `<EMPTY>` into the first slot, and clears parameters/statistics.
It preserves the unique ID and unused instruction/string bytes.

The reconstruction initializes fresh owned storage deterministically; the OEM
constructor did not initialize all unused storage or the unique ID. Instruction
fixtures supply a known initial memory pattern and compare retention after
operations, without claiming that physical OEM heap allocation was zeroed.

Appending an instruction does not validate it. A full buffer is unchanged; the
original logs and continues. String appends check capacity before asserting a
nonempty source, truncate at 47 bytes and retain the tail after the terminator.
Starting a program resets it, stores session type and ID, appends its name, then
checks equality with that name. A long name is therefore truncated before the
original assertion. Setting code asserts a non-null source even for count zero
and asserts when the count exceeds 256. The typed API rejects those inputs and
exposes capacity and assertion outcomes. The OEM assertion helper returns;
the historical comparison stops at that call and does not establish the
remaining OEM behavior for these rejected inputs.

The data member names in `ProgramParameters` are supported by the original
diagnostic function `0x400defe4`: program ID, ABV, IBU, time, water, intensity,
temperature and resumed-session flag. `ProgramStats` follows `0x400d75e0`:
heating rate, duration, pressure, maximum temperature and added seconds.

## Coffee programs and statistics

`src/program_writer.cpp` reconstructs the original cold, hot, hybrid and custom
coffee writer, including screen sequence, valve/pump/filter operations, waits,
drains and program completion sequence. It preserves the cold program's exact
fixed-point temperature `0x30e3` and its 5,400-second hold. Hot/custom steps use
integer Fahrenheit in the original 50–205 range. Intensity is truncated and
clamped to 1–15 minutes. Custom duration is truncated from seconds to minutes;
the original does not validate that quantity's unit before this calculation.
All Fahrenheit/fixed-point rounding and uint32 minute multiplication are retained.

The metadata writer checks water is a volume, copies parameters, computes a
heating estimate of `11.36 / batchLiters`, and computes duration using original
reference wort temperature 12.77 C. It then writes pressure, maximum temperature,
and added seconds. Volumes below 0.1 L reach an original assertion site, where
the typed API and historical comparison stop; subsequent OEM work is not compared.
Static program statistics use the same duration estimator, heating rate 1 C/min,
and estimated boil temperature minus 2 C.

The original forward/reverse iterator estimator is now the shared
`estimate_instructions_seconds` function used by both the writer and runtime.
This avoids duplicating its unusual wait-time, heating, cooling, and unsigned
wrap behavior. The 5,792 runtime comparisons, including their historical
assertion-boundary prefixes, and real runtime→heater→MSP integration were replayed
after extracting this common code. Their scope is recorded in
[program-runtime.md](program-runtime.md).

## Evidence and remaining scope

`tests/fixtures/program-repository-original.json` records **669 original-instruction
scenarios across 34 function bodies**: **538 complete within the stated service
boundaries; 131 stop at an assertion call**. The latter are compared execution
prefixes, not completed OEM operations. The historical harness throws at
`0x400db014` and records the state before that helper executes. The actual helper
logs, increments a diagnostic counter, delays 200 ms, and returns. Its effects
and the caller's subsequent behavior are not covered by those 131 comparisons.

Cases compare active instruction and string data, record fields, parameters,
bit-exact double statistics, assertion lines, represented return values, and
hashes of retained code/string storage at the captured endpoint. They cover
every resolver ID, invalid IDs, buffer boundaries, null/empty/long strings,
partial mutations before assertions, coffee parameter ranges and static/dynamic
duration calculations. Separate integration loads every built-in and a buffered
program through the actual reconstructed runtime. See
[assertion-coverage.md](assertion-coverage.md) for counts and the distinction
between historical prefixes and continuing traces.

`analysis/program-repository-validation.json` pins input/tool/generator/source
identities and execution limits. Target compilation with the pinned Xtensa GCC
5.2 succeeds for catalog, repository, writer and shared estimator. The instruction
harness executes original application routines; only reviewed register-window
boundaries, SDK scalar math, three coffee formatting patterns, ROM memory/string
functions and diagnostics are explicit services. It rejects unreviewed hardware
loops. No hardware I/O is performed.

Important original entries include `0x400dfea0`/`0x400dfe58` (buffer constructor/reset),
`0x400e0e84` (strings), `0x400d752c` (instructions), `0x400e0f18` (start),
`0x400e3e24` (set code), `0x400e423c`/`0x400d7568`/`0x400e4380` (coffee steps),
`0x400ec7b4` (coffee generation), `0x400ec670` (metadata), `0x400ed15c` (static
statistics), and `0x400ec360` (duration).

This milestone does not claim that HTTP recipe decoding, local JSON parsing,
server exchange, UI wiring or the complete application link is finished. Those
are subsequent application dependencies, not successful stand-in callbacks.
