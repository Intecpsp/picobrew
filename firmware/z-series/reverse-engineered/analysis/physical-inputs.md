# Physical button and rotary encoder producers

`physical_inputs.*` reconstructs the producers that feed Core and Head.
The existing `head_encoder.*` handles a consumed rotation event; it does not
sample the hardware or produce that event. These new modules recover that
missing layer from Z119, SHA-256
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.

| Original entry | Recovered operation |
| --- | --- |
| `0x400d0364` | Button GPIO setup and initial active-low observation |
| `0x400ee2fc`, `0x400e5f08` | Button edge/state transition and press counting |
| `0x400f06a4` | Button polling, 5,000 ms long press and 100 ms release interval |
| `0x400f0670`, `0x400f0568` | Button event construction and drop-new one-slot publication |
| `0x400fb8a4` | Encoder console registration, button/GPIO setup and initial phase |
| `0x40080808`, `0x40080828` | Two-pin sample and interrupt ring producer |
| `0x4008c210` | Intentionally empty button-falling interrupt handler |
| `0x400f0804`, `0x400d198c` | Button forwarding, ring decoding, rotation output and activity date |
| `0x400f068c`, `0x400d19ac` through `0x400d1a1c` | Short-press and eight signed console rotation wrappers |

The Core constructor `0x40120a04` binds power button ID 0 to GPIO 32 and
encoder button ID 1 to GPIO 39. Encoder A/B pins are 34/35. Original encoder
offsets are button `+0xb0`, pin bytes `+0xd4..+0xd6`, last activity `+0xd8`,
phase/previous-phase `+0xdc/+0xdd`, 256-byte ring `+0xde`, producer index
`+0x1de`, consumer index `+0x1e0`, and accumulated count `+0x1e4`. Owned types
do not copy this ABI onto the host. The nine-byte phase table is read from
original `0x3f41f03c` in the independent oracle.

## Button behavior

Negative pins disable setup/polling. Other pins narrow to eight bits for SDK
calls. Setup uses the original input mode 1 and observes whether the pin is
low; it does not manufacture an initial edge or reset the state machine.

State 0 is idle. A press enters state 1 and increments the count. A release
enters state 2. A new press during state 2 increments the same count. More
than 100 ms after the last release emits one short event with that count;
more than 5,000 ms after a press emits one long event. Equality does not
trigger either event. Deadlines use wrapped unsigned addition and direct
comparison exactly as the original. An event resets the state/count but
retains the last physical observation, so holding the button after a long
event does not repeatedly emit it.

Events reuse `CoreButtonEvent`: `id` is the button ID, `type` is the original
long-press byte, and `argument` is the press count. `PhysicalButton` accepts
the existing `PendingEvent<CoreButtonEvent>` destination. Occupied slots keep
their earlier payload. No RTOS queue or overwrite policy is substituted.

## Rotary input and overflow

Setup registers console actions, initializes its button, configures the
three encoder pins as inputs, attaches CHANGE (3) to A/B and FALLING (2) to
the button, then samples A/B in that order. The button ISR really is empty
in the original; button events come from polling.

Each A/B interrupt writes `2*A+B` into the current ring slot and advances
the 16-bit producer index, wrapping at 256. **The OEM has no full check.**
Exactly one unread producer lap makes producer and consumer equal, so the
consumer considers the ring empty. Further writes overwrite earlier unread
input. This behavior is preserved, not replaced by a capacity counter.

The tick first polls its button and forwards a pending button event, then
drains the rotary ring. It retains the original nine-state lookup sequence,
half-cycle transition rules, bounce/reversal handling, and signed delta.
One nonzero combined rotation event is emitted after draining, then its
32-bit count is added with wraparound. Console rotation actions emit directly
without changing that accumulated count. The console short-press action
publishes a short event with count one.

Corrupt ring indices or phases that would index outside the original storage
are explicit typed failures in the owned source. Valid-state behavior is
independently compared. Likewise, the concrete platform adapter rejects an
invalid interrupt pin before the historical SDK indexes its unguarded
40-entry callback array. The portable setup still exposes the original
invalid-pin sentinel to its required transport callback.

## Concrete ESP32 adapter and ISR limits

`physical_input_platform.*` uses actual historical `pinMode`, `digitalRead`,
`attachInterrupt` and `millis`. It binds one physical encoder, matching the
original global Core. Its binding and encoder must remain alive in internal
RAM while interrupts are attached. Application-owned console registration
and event consumers are required services, not successful defaults.

The encoder change callback, empty button callback, portable ISR/sample
methods and concrete pin reader are assigned to IRAM. The target checker
also compiles the pinned SDK's actual GPIO implementation: `digitalRead` is
an alias of `__digitalRead`, both reside in IRAM, and they directly read the
mapped GPIO peripheral at `0x3ff44000` without calls. The SDK interrupt
dispatcher resides in IRAM and registration requests `ESP_INTR_FLAG_IRAM`.
Object inspection also confirms `MEMW` before the volatile ring/index stores.

These are target object-section and source checks. They do not establish a
complete linked image, final memory placement of caller-owned objects,
multi-core synchronization, actual interrupt latency, or a cache-disabled
hardware run. The portable fixture is sequential. `volatile` and the Xtensa
implementation boundary preserve the original ISR access pattern; they are
not a portable C++ thread-safety claim.

## Independent verification

All **948 cases across 22 original bodies** match complete state, all 256
ring bytes, returned samples, and ordered functional clock/GPIO/interrupt/
event calls. Coverage includes strict/equal/wrapped button deadlines,
multiple presses, long-hold suppression, occupied mailbox retention, every
valid decoder phase/previous-state/sample combination, bounce/reversal,
index wrap, complete producer laps, partial overwrites, signed count wrap,
and all console wrappers. Seeded sequences are retained with the fixture.

The oracle executes original event creation, queue copying and queue reads.
SDK GPIO/interrupt operations, console registry allocation, typed application
sinks, and diagnostics are explicit boundaries. Diagnostic clock calls do
not advance the deterministic functional clock fixture; their wall time is
not attributed to recovered input behavior. Unknown calls fail execution.

The host integration feeds real physical producers into the recovered Head
rotation/button handlers and Core power-button mailbox. ASan/UBSan checks
cover short/long presses, clockwise/counterclockwise input, ring overflow,
payload retention and invalid-state/service guards.

```sh
python3 tools/validate_physical_inputs.py --prepare-input /tmp/z-inputs.json
JAVA_HOME=/path/to/pinned-jdk /path/to/ghidra/support/analyzeHeadless \
  /path/to/isolated-project firmware -process zseries_0_0_119.bin \
  -noanalysis -readOnly -max-cpu 1 -scriptPath tools/ghidra \
  -postScript EmulatePhysicalInputs.java /tmp/z-inputs.json /tmp/z-inputs-original.jsonl
python3 tools/validate_physical_inputs.py \
  --original-jsonl /tmp/z-inputs-original.jsonl \
  --export /path/to/z119-callback-export --write-evidence
ASAN_OPTIONS=detect_leaks=0 python3 -m unittest discover -s tests -p test_physical_inputs.py -v
# After the normal documented historical object build:
python3 tools/verify_physical_input_target.py --objects /path/to/objects \
  --toolchain /path/to/pinned-toolchain --arduino /path/to/pinned-arduino \
  --output analysis/physical-inputs-target-validation.json
```

Original instructions, retained fixture, owned source, target objects, SDK
GPIO source and the section checker are hash-identified in the two validation
reports. No flashing, installation or physical input operation was performed.
