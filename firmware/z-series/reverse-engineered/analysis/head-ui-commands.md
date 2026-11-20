# Z119 Head UI command dispatcher

`head_ui_commands.hpp/cpp` reconstruct the complete `0x401080d4` dispatcher,
including all 176 original command identifiers and the invalid-command path.
The handler consumes the retained one-slot event, silences the alarm, cancels
the transient timer and applies the original ordered route. Its shared
`HeadNumericState`, `HeadTextState` and `HeadTimer` references keep input handlers
and UI-command handlers on the same storage.

## Concrete routing and ownership

The implementation contains the original Core command and program mappings,
static menu items and localized titles, direct numeric/text input screen
construction, recipe selection/strength state, manual-step defaults and payload,
search retention/clearing, service-mode settings choices and diagnostic display
suspension. It expands the original program-start, recipe-command and recipe
numeric-input helpers through their Core posting and presentation boundaries.
The original `StopAsFinish` identifier (54) has no stop-command branch in this
image: it reaches the diagnostic/synchronization fallback. The implementation
retains that observed behavior.

`HeadUiCommandData` owns the optional recipe reference, program parameters and
search text. `HeadUiCoreEnvelope` is suitable for the existing `PendingEvent`:
its copy constructor and assignment rebind `CoreCommand::detail` to the copy's
own payload. A posting service must materialize this envelope during the
immediate action callback. Retaining the callback's local pointer would create
a dangling payload, even if the mailbox itself were persistent.

The screen request is typed (`HeadUiScreen`) and carries the actual selected
layout, command, response command, duration mode, location, strength, quantity
and character-input options. Named `HeadUiEffect` services identify the original
helper bodies; an absent or failed service remains an explicit error. This
module alone does not supply the complete panel renderer, Wi-Fi service,
configuration persistence or application owner.

## Assertions that return

Missing index/recipe flags, loading a non-coffee parameter set and starting a
zero-duration runtime-menu timer execute the original assertion continuation.
The recipe command helper has two independent flag checks; a missing flag logs
and increments the counter twice before posting the retained recipe. Assertions
must use the shared diagnostic service (log, counter increment, 200 ms delay,
return), not a process abort. The typed source preserves the payload and later
effects even for these cases.

## Evidence

`analysis/head-ui-validation.json` binds this source and its generator to
**1,094 original instruction executions**, preserved with deterministic gzip in
`tests/fixtures/head-ui-original.json.gz`. Coverage includes every command,
all supported languages plus invalid-language fallback, service-mode and T1
menu branches, payload flag/index combinations, all program identifiers,
strength fallback, runtime-menu title/name selection, wraparound/zero-duration
timers and pending/empty/debug event reads. Native comparison includes ordered
effects, localized bytes, full recipe payload hashes, Core command data,
manual/program state, search buffers and assertion counters after continuation.

Original menu population, program/recipe helpers, command constructors, payload
getters, title lookup, display suspension and timer/assertion helpers execute in
the oracle. Named high-level screen/service bodies, Core dispatch/delivery,
alarm and synchronization remain explicit boundaries. ROM copying, formatting
diagnostics, prescribed clock/delay and compiler arithmetic are also bounded
services, not full CPU/RTOS/device emulation.

```
python3 tests/run_head_ui_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_ui.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_ui.py -v
```
