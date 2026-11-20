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
The owned APIs' typed guards remain deliberate bounded behavior at those sites.

| Component | Captured cases | Completed without that stop | Assertion-stopped prefixes | Retained fixture |
| --- | ---: | ---: | ---: | --- |
| Program repository/writer | 669 | 538 | 131 | [program-repository-original.json](../tests/fixtures/program-repository-original.json) |
| Program runtime/quantity | 5,792 | 4,769 | 1,023 | [program-runtime-original.json](../tests/fixtures/program-runtime-original.json) |
| Head numeric inputs | 1,637 | 1,403 | 234 | [head-numeric-original.json](../tests/fixtures/head-numeric-original.json) |
| MSP protocol | 5,553 | 3,741 | 1,812 | [msp-original.json](../tests/fixtures/msp-original.json) |

"Completed" means the selected original routine returned within that fixture's
documented SDK, transport, arithmetic and other service boundaries. It does not
mean a complete firmware execution or physical-device validation.

The counts are reproducible directly from the retained fixture records:

- Repository and runtime: count `cases` entries with nonzero `assertion`.
- Head numeric: count `cases` entries with nonzero `assertion_line`: 210 at
  line `0x2ba` and 24 at `0x349`.
- MSP: count nonzero assertion fields at zero-based indices `guards[*][3]`
  (1,791), `erase[*][1]` (3), `valve[*][2]` (15), and `setup[*][1]` (3).
  The other seven case arrays have no assertion-stop records.

Each total equals its oracle's recorded assertion-boundary call count. The
four validation reports add this classification without changing historical
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

The corrected [Head network](head-network-events.md) and
[Head encoder](head-encoder.md) comparisons also execute the returning helper.
The encoder report separates 3,026 completed matches from 73 deliberate
character-selection guards, retaining the differing OEM continuation.
[HTTP application](local-http-application.md) evidence separates 62 completed
matches from six stricter input rejections. The [recipe analysis](program-recipes.md)
retains its 13 historical first-assertion snapshots and supplies a separate
complete continuation comparison documenting all 13 bounded-policy deviations.

Reconstructing or comparing the later OEM behavior for the historical prefixes
requires additional original execution and a separate review of any retained
typed guard. Passing the existing prefix comparisons alone does not supply
that evidence.
