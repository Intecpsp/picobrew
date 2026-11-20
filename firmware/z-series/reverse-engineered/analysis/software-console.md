# Runtime and PicoStill console diagnostics

`software_console_tests.cpp` reconstructs the complete original self-test bodies,
including their assertions, scope boundaries and diagnostic text. The tests use
the production instruction stack and program catalog, instruction formatter,
PicoStill parser and statistics, concrete Still CSV formatter, and the pinned
JSON serializer and decoder. They are callable from the persistent application
console through `software_console_test(callback, application, services, hooks)`.
Unknown callbacks return failure.

| Original entry | Reconstructed test |
|---|---|
| `400e0e70` / `400e0d34` | Instruction stack, drop monitor, valve enum casts |
| `400eea38` | Sensor presence, speed calculation and unsigned timestamp wrap |
| `400f610c` | Concrete CSV, error state, 10/60 second statistics windows |
| `4010d824` | Empty, decimal, integer, missing-field and error text responses |
| `40119eb8` | Still request serialization and three authorization responses |
| `4011a204` | All four Still tests in their original order |
| `4011a1fc` | Still backend test wrapper |

Fourteen actual original executions compare every diagnostic and scope event,
assertion counter and final clock. Seven cases use a sufficient stack watermark;
seven use the original failing watermark of 700 with both the clock and counter
near wrap. All 129 reached application functions execute their original
instructions. The real assertion helper `400db014` executes 39 times, including
its global counter update and 200 ms delay, then returns to the caller. The
reconstructed results match all fourteen cases.

The original stack-pop self-test first clears its stack. Its loop therefore
performs zero pops; this recovered behavior is retained. The sensor self-test
checks the previous sample after a zero-time update and later replaces that
result with a defined one-minute update. The concrete Still CSV formatter was
extracted from `core_csv_data` without changing its output; the existing
SerialLogger/CSV original comparison was rerun successfully.

The replay validates all mapped segments against the preserved OEM image before
execution. The explicit environmental boundaries are ROM memory and strings,
scalar arithmetic, reviewed numeric/string formatting and decimal parsing,
clock and stack watermark, C++ static guards, diagnostic capture, task watchdog reset
and ScopeLogger text rendering. SDK timing and physical peripherals are not
emulated. No live backend request is made by these tests.

The complete diagnostic-body evidence is separate from malformed production
input continuation evidence. The independent Runtime matrix now completes all
5,792 cases with its returning assertion helper; see `runtime-continuation-validation.json`.
PicoStill parser guard coverage remains separately scoped by its own production
evidence.

Evidence: `software-console-validation.json`, deterministic compressed fixtures,
and `software-console-target-build.json`. The target report contains actual
compilation of the diagnostic module and both SerialLogger/CSV objects with the
pinned ESP32 compiler; it does not claim a complete application link or hardware
test.
