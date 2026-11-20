# Z119 Head text input

`head_text_input.hpp/.cpp` reconstructs the Head text handler at `0x40107578`
and the state changes performed by its original fixed-capacity string helpers.
The API receives the selected event's text and UI command after consumption.
The oracle separately executes and verifies the original mailbox clearing.

| Condition, in priority order | Recovered result |
| --- | --- |
| Awaiting a Wi-Fi password | Clear the flag, replace the password, post `WiFiConnect` with the current credentials, then synchronize |
| `PicoPakRecipeInput` (16) | Convert the input with `atoi`, post `PicoPakRecipeRefLoad` with that reference, and return |
| `ZymaticRecipeInput` (17) | Convert the input with `atoi`, post `ZymaticRecipeRefLoad` with that reference, and return |
| `ZKitRecipeListGetSearch` (6) | Replace the Head-owned kit code, retain at most 16 characters and its truncation flag, then post `ZKitRecipeListGet` with its pointer |
| Other command | Synchronize |

Password entry wins even when the event contains a recipe/search command.
Recipe and kit-code posts do not synchronize afterward. Both password and kit
assignment clear the destination before copying; shorter replacements cannot
retain the previous suffix. The original input buffer holds at most 64 text
characters and a terminator. The source rejects an unterminated 65-byte input
before changing state or invoking a service.

## Command payload boundary

The original recipe reference is at CoreCommand `+0x9c`; its kit-code pointer
is at `+0xa8`. The existing general CoreCommand reconstruction did not yet own
these recipe/backend payload fields. `HeadTextCommand` therefore provides an
explicit typed envelope with the command ID, reference, credentials or kit
code. Its required service must assemble and post the actual Core command.
No missing payload is silently discarded. A serviced original drop-new queue
operation is successful even when an occupied mailbox rejects the new item.

Envelope and credentials pointers are valid during the callback. Kit codes
use Head-owned mutable storage, matching the original pointer's ownership.
The caller must keep that Head state alive while the Core consumes its event.
Missing and failed services return errors, and failure stops later effects.

## Conversion and comparison boundaries

The original calls `0x400566c4`, identified as `atoi` by the pinned SDK's
`tools/sdk/ld/esp32.rom.spiram_incompatible_fns.ld`. The owned source calls the
platform C library's `atoi`; it does not impose a different input grammar.
The ROM code is outside the preserved application image, so conversion is an
explicit oracle boundary. The fixture supplies the decimal result for inputs
whose parsed values fit signed 32-bit integers. These include signs, ASCII
whitespace, leading zeros, trailing nondigits, empty input and both signed
limits. Overflow equivalence across host and target C libraries is not claimed.

In contrast, the original Buffer16/Buffer64 reset, append, length, capacity
and overflow-flag logic executes in the oracle. ROM memory/length operations,
diagnostic output, Core delivery and screen synchronization are modeled or
required service boundaries.

## Validation

**922 deterministic original-instruction cases** match Head state and ordered
command/synchronization outputs. Cases cover all 176 UI command IDs and an
invalid ID, both password-input states, every input length from zero through
64, kit-code truncation/reset, embedded terminators and representable numeric
inputs. Compared fields include retained credentials, kit text, its length
and truncation flag, password state, command ID/reference and pointer ownership.
Source-only tests cover unterminated input and service failures.

```sh
python3 tests/run_head_text_emulation.py \
  --project <byte-verified-z119-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> \
  --output build/head-text-original
python3 tools/validate_head_text.py \
  --original build/head-text-original --write-evidence
python3 -m unittest discover -s tests -p test_head_text.py
```

The fixture/report binds the image, executed function ranges, generator and
native source. Complete Core payload assembly, complete Head presentation,
linked firmware and physical operation remain separate work.
