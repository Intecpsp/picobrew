# Z119 ScreenLibrary tick dispatch

`ui_screen_tick.hpp/.cpp` recovers `0x40103e44` and the nested Still transient
gate at `0x400f181c`. It provides the next concrete application layer below
`HeadFlow`'s required `ScreensTick` service. It retains typed drawing, menu
text-scroll and slideshow services; those child operations still require their
complete implementations.

The combined paragraph/menu layout has a separate path. Its paragraph caret
is updated when enabled and its deadline is **less than or equal to** the
supplied time. The next caret deadline is computed after drawing. The combined
menu's text scroll then ticks, and the handler returns without dispatching any
normal-layout widgets.

The normal path dispatches in this order:

| Order | Operation | Deadline condition and caller update |
| --- | --- | --- |
| 1 | Runtime-state diagnostic drawing | Enabled and deadline `< now`; set `now + 500` after drawing |
| 2 | Still transient drawing | Enabled and deadline `< now`; set `now + 500` after drawing |
| 3 | Menu text scrolling | Called every normal tick with the supplied time |
| 4 | Slideshow | Called every normal tick with the supplied time |
| 5 | Pause drawing | Enabled and deadline `< now`; child owns its deadline update |
| 6 | Runtime transient drawing | Enabled and deadline `< now`; child owns its deadline update |
| 7 | Notification drawing | Enabled and deadline `< now`; child owns its deadline update |
| 8 | Waiting animation drawing | Enabled, animation enabled and deadline `< now`; advance frame and set `now + 500` before drawing |
| 9 | Level drawing | Enabled and deadline `< now`; set `now + 250` after drawing |
| 10 | Paragraph scroll caret | Enabled and deadline `<= now`; add its 16-bit interval after drawing |

The recovered code preserves unsigned 32-bit deadline comparisons and addition,
including the OEM behavior at clock wrap. It does not substitute a different
wrap-aware timer policy. The waiting frame advances by `(frame + 1) % count`,
with the addition promoted before modulo, so frame 255 advances correctly for
each nonzero count.

All services are required when their branch executes. Missing or failed
services produce an explicit error and stop later operations. If an active
waiting animation has zero frames, the source returns `InvalidFrameCount`
before division. This is a bounded repair; the already-written deadline is
retained. No original zero-divisor execution is claimed.

## Validation

**2,148 original-instruction cases** match complete periodic state, waiting
frame values, deadlines and ordered call arguments. They cover all 1,024
enable/animation-flag combinations at both equal and expired deadlines,
combined-layout exclusion, zero and maximum clock values, wraparound,
16-bit caret intervals and byte frame limits. Required-service tests also
verify the different before/after-draw write order when a callback fails.

```sh
python3 tests/run_ui_screen_tick_emulation.py \
  --project <byte-verified-z119-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> \
  --output build/ui-screen-tick-original
python3 tools/validate_ui_screen_tick.py \
  --original build/ui-screen-tick-original --write-evidence
python3 -m unittest discover -s tests -p test_ui_screen_tick.py
```

The checked fixture and validation report bind the original image, executed
instruction ranges, generator and native source. Drawing internals and their
state changes, a complete ScreenLibrary, linked firmware image and physical
display behavior remain outside this comparison.
