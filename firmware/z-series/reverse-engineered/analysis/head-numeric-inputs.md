# Z119 Head numeric input handlers

`head_numeric_inputs.hpp/.cpp` recovers five input-specific Head handlers.
The API accepts an event payload after its selected one-slot mailbox has been
consumed. The original mailbox readers execute in the comparison oracle, which
separately records that each reader clears its pending flag.

| Original entry | Recovered behavior |
| --- | --- |
| `0x4010722c` | Post a temperature command, then synchronize the current screen |
| `0x4010730c` | Post a duration command, then synchronize the current screen |
| `0x40107aa0` | Update the active manual-step location or post a location command |
| `0x40107bb8` | Apply a manual-step or coffee quantity input |
| `0x401077f8` | Apply a recipe ABV or IBU input, preserving original flag priority |

## Parameter mapping and ordering

Manual location input sets the location's edited flag before presenting the
manual-step screen. Quantity IDs 5, 6 and 7 update its temperature, duration and
drain fields, respectively, and set the matching edited flag. Other manual
quantity IDs reach the original assertion at line `0x2ba`.

For the four coffee programs (IDs 20 through 23), quantity IDs 0, 1, 3 and 4
update batch size, temperature, intensity and duration, respectively. Each
input updates both the current parameters and remembered coffee defaults
before showing the coffee screen. The temperature/intensity order is
established by executed field stores, not by the declaration order in an
assertion string. Other coffee IDs reach the assertion at line `0x349`.
Quantity events for other programs only synchronize the current screen.

ABV has priority when both recipe-strength input flags are set. The handler
clears only the selected flag, copies the signed input unchanged, and presents
the recipe-strength screen. When neither flag is set, it only synchronizes.
Temperature command inputs preserve the original float bits; the handler does
not add a clamp or reject nonfinite values that the original merely copied.

## Explicit boundaries and bounded repair

Command delivery and screen presentation are required service callbacks.
Missing or failed implementations return an error. A serviced command post
does not imply that an occupied original drop-new mailbox accepted the value.
The pointer passed to a command callback is valid only for that call.

Invalid quantity IDs return `InvalidQuantity` and retain the original assertion
line. This is a deliberate bounded guard that ends the owned handler at the
assertion site. The OEM helper at `0x400db014` logs, increments a diagnostic
counter, delays 200 ms, and returns. The historical oracle throws before
executing that helper, so it does not compare subsequent handler or screen
effects on invalid quantity paths. Diagnostic formatting and full screen drawing
remain service boundaries. The module does not claim complete Head assembly,
concurrent event delivery, a linked application image or physical device operation.

## Verification

**1,637 deterministic comparisons** execute the original handlers, mailbox readers,
CoreCommand constructors, duration-post helper and coffee-program predicate.
Of these, **1,403 complete within the stated service boundaries** and **234 stop
at an assertion call**: 210 at manual quantity line `0x2ba` and 24 at coffee
quantity line `0x349`. Those 234 are compared execution prefixes, not completed
OEM handler traces. Comparisons check exact input bits, CoreCommand payload fields,
all selected and retained parameter fields, edited/pending flags, assertion
boundaries and ordered screen operations. Coverage includes every program ID, quantity IDs
inside and outside each branch, all manual edited-flag combinations, unusual
float/double bit patterns, signed input extrema and the competing ABV/IBU
flags.

The accepted fixture and report are
`tests/fixtures/head-numeric-original.json` and
`analysis/head-numeric-validation.json`. They bind the preserved image hash,
executed original instruction ranges, fixture generator and validated source.
The historical `fatal_assertion` boundary label is retained in those byte-pinned
artifacts; it describes the old harness stop, not the returning OEM helper.
See [assertion-coverage.md](assertion-coverage.md) for the shared evidence scope.

```sh
python3 tests/run_head_numeric_emulation.py \
  --project <byte-verified-z119-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> \
  --output build/head-numeric-original
python3 tools/validate_head_numeric.py \
  --original build/head-numeric-original --write-evidence
python3 -m unittest discover -s tests -p test_head_numeric.py
```
