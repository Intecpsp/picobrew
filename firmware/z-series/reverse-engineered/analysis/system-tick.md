# System watchdog and loop timing

`system_tick.hpp/.cpp` reconstruct the original Z119 `System::tick` body at
`0x400e5e2c`. `system_tick_platform.*` calls the actual historical ESP32
`esp_task_wdt_reset()` and Arduino `millis()` APIs. The diagnostic sink is a
required, caller-owned binding; it is not replaced by a successful no-op.

## Recovered behavior

The watchdog reset precedes every timing observation. A failed SDK reset
reports the original `System.h:262` assertion. Next, the unsigned comparison
checks `last_date <= now`; a clock wrap or regression reports
`SystemState.h:47` before changing timing fields.

The original assertion helper at `0x400db014` **returns**. It samples the clock,
logs the diagnostic, increments the 32-bit global counter at `0x3ffc2e84`,
waits 200 ms through the SDK delay function, then returns to its caller. Both
assertions therefore allow the timing calculation to continue. A regression
uses the wrapped unsigned interval, can become a new maximum, and can produce
the later maximum-duration warning. A failed watchdog reset does not itself
cause an early return from this OEM body.

The recovered service preserves that continuation and requires an explicit
returning assertion callback. Application composition must provide its clock,
diagnostic, shared-counter and delay effects. Only an unavailable or failed
service binding produces an owned error result. It must not be confused with
an original assertion terminating execution.

After those checks, the body computes the unsigned elapsed interval, updates its
minimum and maximum, and applies the exact recurrence
`average * 0.999 + duration * 0.001`. The two multiply operations and addition
remain separate. The original double constants have IEEE-754 representations
`3feff7ced916872b` and `3f50624dd2f1a9fc`; builds disable fast math and floating
point contraction.

| Field | Offset in original SystemState | Initial value |
| --- | --- | --- |
| Last tick date | `0x48` | `0` |
| Current duration | `0x4c` | `0` |
| Minimum duration | `0x50` | `0xffffffff` |
| Maximum duration | `0x54` | `0` |
| Smoothed duration | `0x58` | `0.0` |

The fields are owned by CoreState. Their initial values are visible in its
constructor at `0x400ef598`, and the System constructor at `0x40110aa8` stores
the state pointer at System offset `0x8c`. The new implementation owns only
these timing fields; it does not duplicate the full SystemState layout.

A new maximum strictly greater than 3000 ms produces a warning. An equal
maximum and an interval of exactly 3000 ms do not. The original samples
`millis()` again for that warning, independently of the tick's `now` argument.
The log boundary receives that timestamp and the actual maximum. ANSI color,
file-name decoration, floating timestamp formatting and console transport
remain diagnostic implementation boundaries. The original format identifies
`SystemState.h:56` and says `DurationMax updated to ... ms`.

## Verification

`system-tick-validation.json` records 228 independently executed original
scenarios, including repeated timing updates, strict warning thresholds,
equal/minimum/maximum intervals, the complete unsigned timer range, failure
precedence, clock wrap assertions and finite double edge cases. Each snapshot
compares exact integer values, all double bits, the watchdog return value,
the global assertion count (including wrap), and the ordered watchdog, clock,
diagnostic and delay calls. The retained portable fixture is
`tests/fixtures/system-tick-original.json.gz`.

The original tick and assertion helper both execute through
`Z119Execution.java`; the helper is not replaced by a terminating exception.
Windowed calls, compiler arithmetic, watchdog, clock, diagnostic formatting
and delay are explicit boundaries. This does not validate a booted ESP32,
watchdog timing, interrupts or the complete Core loop. Missing-service and
log-failure tests additionally check the owned adapter's failure contract.

To reproduce original execution, use a read-only imported Z119 Ghidra project:

```sh
python3 tools/validate_system_tick.py --prepare-input /tmp/system-tick-input.json
"$GHIDRA_HOME/support/analyzeHeadless" "$Z119_PROJECT" firmware \
  -process zseries_0_0_119.bin -readOnly -noanalysis -max-cpu 1 \
  -scriptPath tools/ghidra -postScript EmulateSystemTick.java \
  /tmp/system-tick-input.json /tmp/system-tick-original.jsonl
python3 tools/validate_system_tick.py \
  --original-jsonl /tmp/system-tick-original.jsonl \
  --export "$Z119_INSTRUCTION_EXPORT" --write-evidence
python3 -m unittest discover -s tests -p test_system_tick.py
```

The historical compiler/SDK build is recorded in
`system-tick-target-build.json`. It includes the actual watchdog/clock adapter
and the source and header identities used. The returned callbacks require a
stable `SystemTickBindings` lifetime. Setup must register the calling task with
the watchdog before this tick is used. Full Core ownership, error escalation,
diagnostic/assertion sink composition and application linking remain outside
this slice. The earlier local non-returning-assertion model was corrected
before publication; its historical scratch outputs are not the retained proof.
