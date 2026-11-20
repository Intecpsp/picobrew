# Registration, program download and session logging

`core_helpers.hpp` and `core_helpers.cpp` reconstruct the Z119 control bodies
in `CoreHelpers.h`. These own scheduling and lifecycle state; the actual
HTTP/backend operations remain explicit bindings to the corresponding services.

| Original entry | Reconstructed body |
| --- | --- |
| `0x400fb100` | `tick_registration` |
| `0x4010ca58` | `tick_program_download` |
| `0x4011c7c8` | `SessionLogger::transition` |
| `0x4011cadc` | `SessionLogger::begin` |
| `0x4011c928` | `SessionLogger::tick` |

Registration retains the original strict `deadline < now` check and gives a
new registration request priority over processing a pending result. Success or
expiration disables the retry helper. Pending unregistered results publish
`RegistrationNewToken` or `RegistrationTokenExpired` through the real Core
event binding. The inconsistent already-registered retry path diagnoses the
condition and still calls the backend, as the original does.

Program download clears the completion/error flags before configuring and
starting the client's GET request, and waits for the receiver's completion flag
before clearing `active`. HTTP configuration/GET application return values are
ignored by this original controller; adapter failure is separately reported.

The five session-logging states are Idle, Creating, Waiting, Logging and Ending.
Creating and outgoing writes receive 20-second absolute deadlines. Waiting
receives 30 seconds. A resumed session keeps its retained ID and starts in
Waiting. Timed-out log writes cancel the client before the next state; failed
creation or end requests set the original error flag. The raw unsigned deadline
comparisons and wrap behavior are deliberately retained, rather than replaced
with a different elapsed-time policy.

The original assertion helper returns after a diagnostic, incrementing its
global counter and delaying 200 ms. The reconstruction's required assertion
binding retains those effects. Equal-state transitions diagnose and recheck
before returning; a mismatched returned session ID diagnoses and continues.
Missing service bindings and failed adapter calls are explicit integration
errors, never successful no-ops.

## Evidence

`core-helpers-validation.json` records 1,842 native/original comparisons over
all states, registration/download flags, equality/deadline/wrap boundaries,
resumed sessions, backend readiness, inconsistent IDs and seeded cases.
`EmulateCoreHelpers.java` executes the original callers, nested session-state
transition, Core event reset and actual returning assertion instructions.
The retained fixture is deterministic gzip.

HTTP/backend network work and mailbox storage are modeled service boundaries
in these helper comparisons; their effects are compared as ordered typed calls.
Diagnostics compare source lines, assertion counters and delays, not formatted
serial output. The fixture does not establish full application linking, actual
network behavior, timer/task concurrency or physical brewing.

Reproduce the input with `tools/validate_core_helpers.py --prepare-input`, run
`EmulateCoreHelpers.java` against the byte-verified original Ghidra project, and
pass its JSONL output to `--original-jsonl`. `--write-evidence --export` accepts
the preserved instruction export only after every native comparison passes.
