# Z119 Head Core events and Still mailbox

`head_core_events.hpp/cpp` reconstruct the original Core-event consumer
`0x401095b4`, Core state presentation `0x40106e84`, and Still mailbox read
`0x400f41ec`. These are portable owned event/timer bodies, not a complete
front-panel application. `HeadCoreServices::perform` names the actual screen,
Core delivery and maintenance-query helpers that application assembly must
supply. It reports missing or failed services explicitly.

## Recovered behavior

The Core consumer copies the complete owned `CoreEvent`, including all twenty
recipe references and metadata, then clears the pending flag. Reading an empty
mailbox consumes its retained value after an OEM diagnostic; it does not discard
or fabricate a default event. The Still getter has the same retained-value
behavior. Normal `HeadFlow` calls these consumers only when pending.

Core event dispatch preserves registration/expiration, program readiness,
recipe list, boiler detection, loading/network/session errors, rinse/clean
requests and the power-button confirmation flow. State presentation includes
startup, connection/registration messages, download progress, standby display
clearing, resumed-session and maintenance prompts, session start, runtime and
system error screens. Titles and command labels use the recovered localized
catalog. Callback event/string pointers are borrowed for the immediate call;
any queued renderer must copy them.

Download progress retains the original 32-bit signed multiplication, positive
denominator condition, division and conversion to the low byte. Values above
100 call the real diagnostic path and then still publish the progress screen.
Likewise a zero-duration power confirmation calls the assertion and then starts
an active zero-duration timer with the current clock as its deadline. The
assertion callback must log, increment the shared counter and wait 200 ms before
returning. Replacing this callback with a throwing/assert-abort implementation
would change the original continuation.

## Independent execution evidence

`analysis/head-core-validation.json` and the deterministic compressed fixture
`tests/fixtures/head-core-original.json.gz` bind source and generator hashes to
559 executions from the unchanged 0.0.119 application. The cases cover all Core
states and event identifiers, invalid identifiers/languages, previous-state
choices, both maintenance-query outcomes, signed and wrapped progress inputs,
every power-state/timer combination, pending/empty/debug mailbox reads and
Still retained values. Comparison includes ordered presentation operations,
localized bytes, full canonical event payload hashes, timer/progress/dirty
state and the actual original assertion counter.

The original assertion function executes through return; clock, delay, logging,
ROM copying and compiler arithmetic have named boundaries. Presentation helper
bodies and the virtual maintenance query are also explicit boundaries in this
fixture. The repository's existing widget/catalog/renderer evidence is separate;
this fixture does not establish complete screen rendering or physical display,
boot, brewing or RTOS behavior.

Reproduce with the pinned Ghidra/JDK project:

```
python3 tests/run_head_core_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_core.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_core.py -v
```
