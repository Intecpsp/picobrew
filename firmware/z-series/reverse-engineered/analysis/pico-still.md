# Z119 PicoStill controller

`pico_still.hpp` and `pico_still.cpp` recover the original PicoStill application
controller from the preserved Z 0.0.119 image. This module owns its state,
single-slot command and Wi-Fi mailboxes, scan selection, network handoff,
authorization gate, command retry and sensor-poll scheduling. Its outgoing HTTP,
StillClient, authorization backend and runtime gate are explicit required service
interfaces. They are not successful no-ops or claims of complete networking.
The separately recovered [StillClient protocol](still-client.md) and
[outgoing HTTP engine](http-client.md) now bind the first two interfaces;
authorization and complete application assembly remain required.

## Original mapping

| Original entry | Recovered operation |
| --- | --- |
| `0x400fc080` | Console registration and Idle-state setup assertion |
| `0x4011ae0c` | HTTP, StillClient, authorization, then controller tick order |
| `0x4011a818` | Event selection and state-dependent command/response processing |
| `0x4011a378` | State transitions and ordered entry operations |
| `0x4011a66c` | Command retry and exhausted-retry destination |
| `0x4011a78c` | Successful/error response completion and retry selection |
| `0x400f8300` | SDK scan enumeration, prefix filtering and first-eight result limit |
| `0x400f4424` | Wi-Fi mailbox consumption |
| `0x401bf918` | Successful client completion predicate |

Core constructs this controller at `Core+0x4a18`, with the startup secondary
interface at `Core+0x4a1c`. Within it, `+0x150` points to the shared Still state,
`+0x214` is the runtime acquisition/release interface, `+0x218` the Wi-Fi manager,
`+0x21c` the outgoing HTTP service, `+0xa68` the authorization client and `+0xcdc`
the StillClient. The recovered types express these relationships without
recreating a compiler-specific ESP32 C++ object layout.

## Preserved behavior

Wi-Fi events take precedence over commands. A Stop command remains pending
when a Wi-Fi event is consumed on the same tick. Once consumed, Stop goes from
Running to Stopping and from every other non-Idle state to Idle; it returns
before the normal state switch. Unhandled nonzero commands and Wi-Fi events
retain the original returning assertion line numbers.

Scan starts only when the runtime gate permits it. Matching network names use
the original `PicoStill_` prefix; identifiers are truncated to 16 bytes and the
first eight matches are retained. SDK scan counts are signed and SDK SSID
indices narrow to eight bits. The controller copies the configured home
credentials when entering SelectingStill or Configuring. It authorizes the
selected device over the home connection, requests a cleaned acknowledgement
when required, connects to the Still access point with the original password,
retrieves the version/IP and configures home credentials when the reported IP
is exactly `0.0.0.0`. The subsequent LAN handoff and version check lead to
PendingStart, where the runtime gate is released.

The default command retry maximum is two; the version request on the Still
access point permits three retries, as does the separate Still-access-point
connection retry counter. Exhausting the End command returns to Idle; other
exhausted commands enter Error. The original returning assertions for negative
retry counts/limits remain visible. Counter increments use explicitly defined
32-bit wrap before signed comparison, matching the emitted instructions.

Running issues Data only with no command pending, polling enabled and
`next_poll_date < now`. Equality does not trigger a request. Completion sets
the next deadline to `now + 2000` with unsigned wrap. Retryable failures stop
polling only after the fifth consecutive failure with the default threshold
of four. Non-retryable errors stop it immediately. Reconnect clears that latch
and counter, then schedules another poll. Fan commands retain their original
numeric correspondence to StillClient FanOn/FanOff. Stopping disables retries
when the polling-stopped flag is already set.

## Validation

`tools/ghidra/EmulatePicoStill.java` executes the preserved Xtensa instructions
using `Z119Execution.java`; it does not use the recovered implementation to
calculate expected outputs. `tools/validate_pico_still.py` compares complete
state, mailbox flags, stored text bytes and ordered external effects against a
separately compiled C++ bridge. All **2,284** deterministic cases match across
**28 original bodies**, including the original string and mailbox helpers.
The compressed JSON portable fixture retains **2,156** cases; the remaining 128 seeded scenarios
are independently regenerated when accepting fresh original execution evidence.

Coverage includes every transition pair through the original count sentinel,
state/command and state/Wi-Fi combinations, event priority, authorization
results, retry limits and signed edges, callback completion/error combinations,
all eight valid selections, negative/large scan counts, name truncation,
polling threshold and unsigned deadline boundaries. The input-image digest,
input scenarios, original execution, instruction exports and accepted source
hashes are recorded in `pico-still-validation.json`.

The additional ASan/UBSan integration test drives the recovered controller,
actual Wi-Fi credential/target operations and actual Core mailboxes through
scan, authorization, acknowledgement, Still AP configuration, LAN handoff,
start, polling, fan commands, reconnect and Stop. It also checks missing/failed
services and the narrowly repaired invalid-selection case. The pinned historical
Xtensa GCC 5.2 / Arduino ESP32 1.0.1 object build compiles this source in the
33-module archive; `pico-still-target-validation.json` records that evidence.

```sh
python3 tools/validate_pico_still.py --prepare-input /tmp/pico-still-input.json
# Run EmulatePicoStill.java in the mapped Z119 Ghidra project, with the input
# above and a chosen output JSONL path, using -noanalysis -readOnly.
python3 tools/validate_pico_still.py \
  --original-jsonl /tmp/pico-still-original.jsonl \
  --export /path/to/z119-callback-export --write-evidence
ASAN_OPTIONS=detect_leaks=0 python3 -m unittest discover -s tests -p test_pico_still.py
```

The original indexes the fixed scan array without a bounds check. Negative or
out-of-range indexes would read outside that array; the reconstructed API
returns `InvalidSelection` for those undefined cases. Valid indexes 0–7 retain
the original behavior, including access to entries beyond the current result
count. Configuration copies terminate the fixed EEPROM fields explicitly;
malformed EEPROM without a terminator is outside the original defined-string
domain. External scan callbacks must provide a terminated SDK-sized SSID.

These checks cover the controller at the documented boundaries. They do not
execute live sockets, cloud authorization, Still sensor parsing, radio hardware
or the complete original application, and do not qualify a physical machine.

## Complete diagnostic continuation

`still-continuation-validation.json` extends the controller evidence to 2,288
complete OEM executions. The shared client/parser cohort brings the combined
proof to 6,480 cases, 62 actual original function bodies, 1,866 real returning
assertions and 4,557 exact diagnostic messages. All service calls, messages,
state values, final counters and wrapping clocks are compared in order. The
original assertion helper at `0x400db014` runs its own counter update and the
explicit SDK delay boundary advances time by 200 ms before execution resumes.
Mapped OEM bytes and the pinned analysis tools are verified independently.

`PicoStillServices::diagnostics` binds the application's real assertion and log
callbacks. State changes now preserve the original redundant/invalid-state
messages, retry diagnostics, scan truncation/list-limit warnings, and ignored
command/Wi-Fi assertions and messages. A completely empty diagnostic service
retains the earlier value-only standalone API. A partially bound or failing
service returns `MissingService` or `ServiceFailure`; it is not treated as a
successful assertion or log operation. The original out-of-range selection
pointer arithmetic remains an explicitly rejected input in the portable API.

Reproduce the native comparison with
`python firmware/z-series/reverse-engineered/tools/validate_still_continuation.py --write-report`.
The companion `tests/run_still_continuation_emulation.py` regenerates the original
fixtures from a supplied verified Ghidra project, JDK and Ghidra installation.
`still-continuation-target-build.json` records compilation of the five affected
sources with the pinned ESP32 compiler; physical devices and live networks are
outside this evidence.
