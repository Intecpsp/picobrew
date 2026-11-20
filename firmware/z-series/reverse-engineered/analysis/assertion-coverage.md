# Assertion evidence coverage

The Z119 assertion helper at `0x400db014` samples the clock, logs, increments
the 32-bit diagnostic counter at `0x3ffc2e84`, delays 200 ms, and **returns**.
Reaching that call does not by itself establish that the caller terminates.

## Historical comparisons that stop at the call

The following retained oracles throw an exception when execution reaches
`0x400db014`. A nonzero assertion marker therefore identifies a compared
execution prefix ending before the helper executes. The fixture can verify
state changes and service calls up to that endpoint, but cannot establish the
helper's effects, the caller's later behavior, or its final return value.
The historical records remain unchanged. Later continuation suites below
separately establish which current paths continue and which public interfaces
retain an explicit bounded-input policy.

| Component | Captured cases | Completed without that stop | Assertion-stopped prefixes | Retained fixture |
| --- | ---: | ---: | ---: | --- |
| Program repository/writer | 669 | 538 | 131 | [program-repository-original.json](../tests/fixtures/program-repository-original.json) |
| Program runtime/quantity | 5,792 | 4,769 | 1,023 | [program-runtime-original.json](../tests/fixtures/program-runtime-original.json) |
| MSP protocol | 5,553 | 3,741 | 1,812 | [msp-original.json](../tests/fixtures/msp-original.json) |

"Completed" means the selected original routine returned within that fixture's
documented SDK, transport, arithmetic and other service boundaries. It does not
mean a complete firmware execution or physical-device validation.

The counts are reproducible directly from the retained fixture records:

- Repository and runtime: count `cases` entries with nonzero `assertion`.
- MSP: count nonzero assertion fields at zero-based indices `guards[*][3]`
  (1,791), `erase[*][1]` (3), `valve[*][2]` (15), and `setup[*][1]` (3).
  The other seven case arrays have no assertion-stop records.

Each total equals its oracle's recorded assertion-boundary call count. The
retained validation reports add this classification without changing historical
fixtures, generator identities, or the source checkpoint. Source identities
are refreshed only after the corresponding native comparisons are replayed.
Historical labels such as `fatal_assertion` remain in the byte-pinned artifacts;
they describe the old harness stop, not the behavior of the OEM helper.

## Continuing traces

A completed comparison that encounters an assertion must retain the caller's
execution after the helper returns. The corrected [System timing](system-tick.md)
and [Buzzer/alarm](buzzer.md) oracles execute the actual helper, verify its
counter increment and 200 ms delay call, and continue the original caller.
Their reports separately document clock and diagnostic service boundaries.
Those continuing traces do not extend the historical prefix coverage above.

The [Head numeric](head-numeric-inputs.md) comparison now completes all 1,637 cases,
including 234 that execute the original helper and continue through the
subsequent editor behavior. Its compressed replacement fixture records the
diagnostic counter and delay; these cases are no longer assertion-stopped
prefixes. The [Display console](display-console.md) comparison similarly
executes the empty-text assertion and the complete following render path.

The [auxiliary sensor](auxiliary-sensors.md) suite completes all 641 cases,
including 32 real assertion-helper executions and subsequent calibration/read
work. The [ADC acquisition](sensor-acquisition.md) continuation suite adds 255
complete comparisons with 597 real helper executions. It includes the Core
publication loop and the later ordered service calls, preserving the helper's
effects on subsequent channel clock samples. The legacy data-only sensor APIs
remain separately documented guarded interfaces.

The corrected [Head network](head-network-events.md) and
[Head encoder](head-encoder.md) comparisons also execute the returning helper.
The encoder report separates 3,026 completed matches from 73 deliberate
character-selection guards, retaining the differing OEM continuation.
[HTTP application](local-http-application.md) evidence separates 62 completed
matches from six stricter input rejections. The [recipe analysis](program-recipes.md)
retains its 13 historical first-assertion snapshots and supplies a separate
complete continuation comparison documenting all 13 bounded-policy deviations.

## Completed continuation suites

The current source has additional full-return comparisons. These reports keep
the actual assertion helper, its counter update and its delay in the execution
trace, then compare the caller's remaining state changes and service calls.

| Suite | Complete cases | Returning assertions | Coverage |
| --- | ---: | ---: | --- |
| [Runtime](runtime-continuation-validation.json) | 5,792 | 1,142 | Every original Runtime input, including all 1,023 previously stopped cases; exact state bits, stack, effects, counter and clock. |
| [MSP](msp-continuation-validation.json) | 123 | 320 | Production transport, BSL/setup/update recovery and every named MSP self-test; ordered bytes, diagnostics, watchdog resets and final state. |
| [EEPROM](eeprom-continuation-validation.json) | 2,363 | 391 | Complete data/lifecycle matrix, normalization, commit and command registration. |
| [Still controller/client](still-continuation-validation.json) | 6,480 | 1,866 | State transitions, parsing, statistics, protocol errors and subsequent services. |
| [Runtime/Still diagnostics](software-console-validation.json) | 14 | 39 | Complete self-test bodies, including counters, delays and nested diagnostics. |
| [Backend diagnostics](backend-console-validation.json) | 47 | 49 | Actual JSON/program operations, reports and suite markers; two malformed OEM vararg log statements have narrowly documented differences. |
| [Basic diagnostics](basic-console-validation.json) | 152 | 651 | All 37 leaves and the aggregate/wrapper; includes the reused backend, EEPROM, ADC, Runtime and Still bodies. |
| [Azure diagnostics](azure-console-validation.json) | 40 | 66 | Complete integration/password bodies, transition and backoff behavior, real HMAC comparison, counters, delays and watchdog resets. |
| [Network/Int/All and BinaryDownload](network-console-validation.json) | 64 | 148 | Complete orchestration and binary diagnostic bodies; nested child-test calls are explicit boundaries backed by their separate leaf proofs. |
| [Live HttpBackend diagnostic](http-backend-network-validation.json) | 12 | 231 | All nine requests execute through the original HTTP client, receiver, JSON parsers and program builders; 22,810 ordered events and final state match. |

The [outgoing HTTP continuation](http-client-continuation-validation.json)
suite completes 972 cases and the separate [live HTTP diagnostic](http-client-console.md)
completes 55 scenarios. The category/result display comparison also executes
the empty/null message assertion, preserves the text context and compares the
following framebuffer transfer. These are distinct proofs with explicit
environment boundaries; their case and assertion totals must not be added
together as a count of unique original paths.

The targeted MSP continuation suite does not relabel every historical MSP
prefix as a complete replay. Any prefix without a corresponding full-return
comparison retains its original evidence limit. Production recipe/program
input guards also keep their documented stricter rejection policies. Passing
a historical prefix comparison or a self-test alone does not establish those
uncompared continuations.
