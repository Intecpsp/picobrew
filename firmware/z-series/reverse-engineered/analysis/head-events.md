# Z119 Head runtime and button event handlers

`head_events.hpp/.cpp` reconstructs two event-specific Head bodies that were
explicit service boundaries in the earlier outer-flow reconstruction.

| Original entry | Recovered behavior |
| --- | --- |
| `0x40107984` | Runtime transition, update and notification handling |
| `0x4010718c` | Encoder-button short and long presses |
| `0x400f33d4` / `0x400f077c` | Original one-slot Runtime/Button mailbox reads, executed in the oracle |

The owned API accepts the event after the caller consumes the selected mailbox.
`HeadFlow` already requires its event service to perform that selection and
consumption. These handlers do not introduce a second queue or claim to finish
the complete Head/ScreenLibrary assembly.

## Runtime alarms and notification ordering

Runtime transitions produce no Head action. Runtime updates set the alarm
before refreshing the running screen. The original policy is preserved:

- `WaitT2Safety` and `WaitPumpOk` enable the alarm regardless of Runtime state,
  pause reason or selected screen.
- Other instructions enable it only for a running `WaitUser` instruction.
- A user pause silences that alarm. A program pause enables it only for the
  original seven screen values: None, BrewCompleted, PakBrewCompleted,
  ChillCompleted, PicoStillBoilPointFound, CoffeeCompleted1 and StepCompleted.
- Other pause reasons retain the alarm while running `WaitUser`.

A notification first enables the alarm, then retains its notification ID, then
shows the notification screen. Unknown event types produce no UI action; the
OEM diagnostic is an explicit logging boundary.

The alarm service is the recovered `HeadFlow::alarm` operation, including its
timeout. The source does not replace it with a GPIO-only toggle. The new
handlers require implementations of their alarm and presentation services and
propagate failures instead of reporting success for absent implementations.

## Button behavior

The original diagnostic identifies the event byte at `+8` as a long-press flag.
Long presses are discarded. A normal short press goes to ScreenLibrary. When
the Head's encoder-press-hijacked flag is set, a short press clears it and
synchronizes the current screen instead. A long press leaves that flag intact.

## Verification

**13,812 deterministic cases** execute the two original handlers and both
original mailbox readers in the preserved Z119 program. Cases cover all
Runtime states and opcodes, every pause reason and screen combination for
`WaitUser`, both special safety waits, notification IDs, invalid enum values,
and all long-press/hijack combinations. Native source matches the ordered
alarm/screen effects, retained notification ID and hijack state.

The fixture also records that the original mailbox reader clears the pending
flag. This observation is not attributed to the new API, which receives an
already consumed event. Alarm timer internals, screen bodies, diagnostics and
the sequential Xtensa calling convention are explicit execution boundaries.
There is no physical hardware, concurrent event-delivery or complete-image
claim.

```sh
python3 tests/run_head_events_emulation.py \
  --project <byte-verified-z119-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> \
  --output build/head-events-original
python3 tools/validate_head_events.py \
  --original build/head-events-original --write-evidence
python3 -m unittest discover -s tests -p test_head_events.py
```

Validation checks the input image, original instruction ranges, tool and
generator identity, full deterministic case count, and exact native results.
Normal CI replays the accepted fixture and compiles the module in both the host
archive and the historical ESP32 object archive.
