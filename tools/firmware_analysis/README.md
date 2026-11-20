# Reproducible firmware decompiler evidence

`ghidra_export.py` imports an explicitly mapped binary into pinned Ghidra 12.2,
runs bounded single-CPU automatic analysis, and exports address-traceable C
pseudocode, decoded instructions, references, calls, and strings. It does not
run the firmware or touch a device.

**Generated decompiler C is analysis evidence, not original source, a buildable
reconstruction, or proof of behavior.** Names, types, calling conventions,
function boundaries, and hardware effects need manual recovery and comparison
with the original instructions. A completed decompilation can still contain
warnings or incorrect semantics. The number of functions recognized is not an
exhaustive function count or percentage of the application recovered.

## Inputs and tool distributions

Use the preserved OEM image or a decoded payload whose hash is established by
the model's package decoder. A reviewed layout must explicitly specify every
loaded segment's file offset, virtual address, size, and read/write/execute
permissions. Headers, checksums and unmapped padding stay outside memory.
Zero-length ESP32 records remain in the layout but do not create memory blocks.
Overlapping regions require a separately reviewed overlay design and are
rejected by this runner.

The supported analysis processors are `ARM:LE:32:Cortex` with Thumb context and
`Xtensa:LE:32:default`, both using Ghidra's `default` compiler specification.
Selecting a processor does not establish the appliance's physical identity or
validate the compiler's calling-convention model.

The runner verifies these exact Linux x86-64 distributions before launch:

| Distribution | SHA-256 |
| --- | --- |
| [Ghidra 12.2](https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/ghidra_12.2_PUBLIC_20261016.zip) | `2c2cd776db3c1f6a097ba418922378ac3649cadd5e844a6f5f088ae628a7be67` |
| [Temurin JDK 25.0.5+8](https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.5%2B8/OpenJDK25U-jdk_x64_linux_hotspot_25.0.5_8.tar.gz) | `68982529869257d0ee6b824ec5aa270ff8b118bb438670da0b97ebdab178d7fb` |

Keep archives next to their extracted directories or pass `--ghidra-archive`
and `--java-archive`. The manifest also records hashes of installed processor
definitions, compiled language descriptions, core analysis JARs, decompiler,
Java executable/runtime, and the exact copied pipeline scripts. Executable
permissions must be preserved when extracting the distributions.

## Layout example

This illustrates the schema; replace the hash, lengths, entry points, and
addresses with evidence from the actual image. Do not use it as a flash layout.

```json
{
  "schema_version": 1,
  "input_sha256": "64 lowercase hexadecimal characters from the verified payload",
  "language_id": "ARM:LE:32:Cortex",
  "compiler_spec": "default",
  "thumb": true,
  "segments": [
    {"name": "application", "offset": 0, "address": "0x20004000", "size": 156536,
     "read": true, "write": true, "execute": true}
  ],
  "entry_points": ["0x20017b14"],
  "labels": [
    {"address": "0x20017b14", "name": "reset_handler", "function": true,
     "evidence": "Decoded reset vector with Thumb tag removed", "confidence": "address-only"}
  ]
}
```

Addresses may be unsigned integers or prefixed hexadecimal strings. Cortex-M
function/entry addresses omit the low Thumb tag; the pre-script sets Thumb
context across executable regions. Label evidence is retained verbatim in the
manifest. A label is not a recovered function signature or semantic proof.

## Run

```sh
python3 tools/firmware_analysis/ghidra_export.py \
  --input path/to/verified-image.bin \
  --layout path/to/reviewed-layout.json \
  --output path/to/new-export-directory \
  --ghidra-home /path/to/ghidra_12.2_PUBLIC \
  --java-home /path/to/jdk-25.0.5+8
```

The output path must not exist. Every run uses a fresh project and snapshots
its scripts before launch. `JAVA_HOME`, `PATH`, and locale are set only for the
child process. Defaults bound automatic analysis to 600 seconds, each function
to 20 seconds, the entire process group to 3600 seconds, and the inventory to
20,000 functions. These limits can be changed within explicit upper bounds.
An automatic-analysis timeout is recorded in the output and returns exit 2;
launch/export failures return exit 1. A separate `.headless.log` next to the
output retains diagnostic messages and timings and is not deterministic evidence.

Use `--project-output /path/to/new-cache-directory` to retain the analyzed
Ghidra project for further manual typing, instruction emulation, or headless
scripts. Its project name is `firmware`; the program name is the input filename.
That cache contains incidental database state/timestamps and is not part of the
deterministic export or a source deliverable. Leave it in an ignored build/cache
directory. Without this option the project is deleted after export.

## Output and limits

| File | Meaning |
| --- | --- |
| `manifest.json` | Exact input/layout/tool/script hashes, limits, and exported-file hashes |
| `ghidra-summary.json` | Processor, memory blocks, analysis options, seed outcomes, timeout/errors, explicit incomplete-recovery flags |
| `functions.json` | Recognized entries, ranges, names and their provenance, calls/callers, decompiler result and instruction-file paths |
| `decompiled/fn_<address>.c` | Per-function annotated Ghidra C pseudocode; never silently substituted for failed functions |
| `instructions/fn_<address>.json` | Original instruction bytes, decoded operands, flow targets, and references within recognized function bodies |
| `defined-strings.json` | Ghidra-defined strings and reference addresses; not a complete scan of all printable bytes |

Recognized ranges can miss indirectly reached code, merge tail calls, or split
one source routine. Decompiler errors and warning text remain visible. SDK
identification and manual naming should record evidence separately; repeated
bytes alone do not establish equivalent relocated globals or peripheral I/O.

Offline pipeline checks use the Python standard library. The optional Xtensa
ELF matcher and its negative controls also require `pyelftools`:

```sh
python3 -m unittest discover -s tools/firmware_analysis -p 'test_*.py'
```

The pipeline follows the official [headless analyzer documentation](https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/RuntimeScripts/support/analyzeHeadlessREADME.md),
[memory API](https://ghidra.re/ghidra_docs/api/ghidra/program/model/mem/Memory.html),
and [decompiler API](https://ghidra.re/ghidra_docs/api/ghidra/app/decompiler/DecompInterface.html).
Model-specific envelope decoding, ESP32 segment verification, naming, typed
reconstruction, builds, and behavioral tests stay under that model's directory.

## Xtensa SDK function candidates

`compare_xtensa_function.py` compares instruction exports for one ELF object
symbol and one linked-image function. It checks every instruction's bytes
against its input, rejects empty/overlapping exports and reference instructions
outside the symbol, and preserves registers, immediates, and internal branch
destinations. It accounts for density instruction suffixes and adjacent
`l32r`/`callx8` relaxation of a direct call. Literal constants and referenced
string bytes must match; repeated relocated external references must resolve
consistently.

The output identifies structural candidates and records inferred external
bindings. It explicitly does not prove external callee equivalence, instruction
coverage, complete function behavior, the original source revision, or physical
compatibility. The negative controls exercise changed registers/constants,
corrupt instruction bytes, truncated function extents, unmapped literals,
duplicate/empty exports, incorrect call-register pairing, and changed branches.

```sh
python3 tools/firmware_analysis/compare_xtensa_function.py \
  --image verified-oem.bin --manifest original-export/manifest.json \
  --object sdk-function.o --symbol function_name --reference-base 0x10000 \
  --reference reference-export/instructions/fn_00010000.json \
  --original original-export/instructions/fn_40100000.json \
  --output candidate-comparison.json
```
