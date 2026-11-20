# Core event payload and recipe readiness publication

`program_list.hpp` and `program_list.cpp` define the original `CoreEvent`
payload, its twenty recipe references, the one-slot mailbox and the concrete
notification used after local recipe parsing. This object is shared by Core,
the recipe/backends and Head; its `type` is a `CoreEventType`, not a backend
state identifier.

The original Core constructor places the reusable event at Core `+0x2f00`
and the Head mailbox at Core `+0x5fd8`. The original `0x400d06b4` description
formatter establishes the event and tail-field meanings. The reference is
Z119, SHA-256 `d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.

| Original offset | Owned field |
| --- | --- |
| `+0x04` | `CoreEventType type` |
| `+0x08`, `+0x0c` | Two event argument words |
| `+0x10` | Twenty `RecipeReference` records, original stride `0xb0` |
| `+0xdd0`, `+0xdd4`, `+0xdd8` | Recipe count, list offset and total |
| `+0xddc` | Preserved boolean `byte_ddc`; meaning not established here |
| `+0xde0`, `+0xde4` | HEX detection error and boiler type |
| `+0xde8`, `+0xdec` | Core error and originating Core command |

Each recipe reference owns its ID, 64-character bounded name and buffer
metadata, recipe kind, and the existing recovered `ProgramParameters`.
The application uses portable types; original layout offsets appear only
in evidence and test translation.

## Initialization, reset and publication

Original `0x400eed28` initializes one recipe reference, and `0x400eef6c`
initializes the event and all twenty references. The original event
constructor leaves both argument words untouched. New C++ objects initialize
them to zero to avoid exposing indeterminate storage, while the explicit
`initialize()` method preserves existing argument values. The comparison
tests that preservation using nonzero seeded arguments.

`reset(type)` implements `0x401bf484`: it changes the type and clears all
tail metadata, selecting unknown boiler type, while retaining both arguments
and all recipe references. This distinction matters when an event object is
reused after downloading a recipe or list.

Original `0x400f02e4` copies the complete payload into an empty mailbox and
sets its pending flag. If an event is already pending, publication leaves
the earlier value intact. `CoreEventMailbox` reuses the recovered
`PendingEvent<CoreEvent>` implementation, including its value ownership.

`LocalRecipePublication` supplies a concrete `RecipeNotification` for
`parse_local_recipe`: successful parsing resets the live event to
`ProgramLoaded` (7), clears its originating command and publishes a copy.
It preserves the original behavior when the mailbox is already occupied.
Program data remains owned by the existing `ProgramBuffer`; the event does
not invent a second program repository.

## Evidence and integration

All **27 original-instruction scenarios** match complete payload hashes
after each operation, including the recipe-name bytes, quantities and
metadata. The constructor, bounded buffers, reset and mailbox copy execute
as original instructions. Only ROM memory operations, diagnostics and
single-thread memory ordering are service boundaries. The vtable word is
excluded from the portable payload comparison; original object padding is
zero-seeded, and the test translator does not redefine application layout.

Portable tests cover the independent goldens, source identity and concrete
notification ownership: changing the source after publication leaves the
queued names intact, a second publication preserves the pending event, and
taking that event permits the subsequent readiness notification. The scoped
target report verifies compilation with pinned Xtensa GCC 5.2. No concurrent
mailbox behavior or complete application link is claimed.

Head's original Core-event consumer reads the same payload; its reconstruction
can consume `CoreEventMailbox` directly. Outgoing HTTP backends can populate
the same event and recipe-reference array, avoiding competing payload types.

```sh
python3 -m unittest discover -s tests -p test_core_event.py -v
python3 tools/validate_core_event.py --prepare-input /tmp/z-event-input.json
analyzeHeadless <project-directory> firmware \
  -process zseries_0_0_119.bin -noanalysis -readOnly \
  -scriptPath <this-directory>/tools/ghidra \
  -postScript EmulateCoreEvent.java /tmp/z-event-input.json /tmp/z-event-original.jsonl
python3 tools/validate_core_event.py \
  --original-jsonl /tmp/z-event-original.jsonl \
  --export <preserved-instruction-export-directory> --write-evidence
```
