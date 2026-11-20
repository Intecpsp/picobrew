# Z119 Head encoder and inline widget selection

`head_encoder.hpp/.cpp` recovers the encoder handler at `0x400fdd0c`, the
integer quantity-selector update at `0x400f1eb4`, and the timer-reset behavior
at `0x400d1cbc` through the previously recovered `head_timer_reset` function.
The source receives the payload after mailbox consumption. The original
comparison executes the handler's mailbox reads and records its cleared
pending flag separately.

The handler reverses the input delta. In normal mode it processes all enabled
widgets in the original order: menu, slideshow, paragraph, text character,
temperature, duration, strength, the two quantity selectors, and diagnostic
paragraph. It then resets the Head transient timer. These are independent
conditions, not an exclusive choice of one widget. Combined paragraph/menu
mode bypasses that normal sequence and retains its original byte-width count
and selection arithmetic.

## Recovered selection rules

| Control | Original rule |
| --- | --- |
| Menu | Clamp selection using the original signed lower and unsigned upper tests; update only when the visible-row count is nonzero |
| Slideshow | Unsigned modulo of count, selection and reversed delta; select a changed slide, then set its timer duration to 60 seconds and reset the timer |
| Paragraph | Clamp its scroll offset to zero through total lines minus visible lines |
| Character | Correct a negative index once by adding the character count; subtract the count once at the upper end; preserve the original negative-index assertion |
| Temperature | Apply signed lower then upper bounds, including the original behavior for inverted bounds |
| Duration | Round down to the current increment, apply the delta, and clamp to zero through 259,200 seconds |
| Strength | Clamp to levels 1 through 10 |
| Quantity selectors | Clamp to first/last item, or wrap once to the opposite endpoint when wrapping is enabled |

Duration increments are 60 seconds below one hour, 300 below three hours, 900
below twelve hours, and 3,600 thereafter. All arithmetic preserves the
original 32-bit wrap while avoiding signed-overflow undefined behavior in C++.
Menu, character, temperature and strength dirty-flag stores occur after the
corresponding drawing callback, in their observed original order.

## Explicit boundaries and repairs

Drawing, slideshow selection, combined paragraph/menu selection, quantity
change notification and diagnostic paragraph presentation are required
callbacks. A slideshow or combined-selection service must implement the child
operation's state changes as well as presentation. The new dispatcher does
not claim that an empty callback implements those children. Its exact
comparison checks their ordered calls and arguments; those child bodies are
not executed in this oracle.

The timer reset is concrete: an inactive timer makes no clock call, while an
active timer computes its deadline using the current clock and unsigned
32-bit addition. Missing clock or action services return an error.

The original assertions for zero delta (`0x2c2`) and a negative character
selection after the single-wrap correction (`0x481`) become typed errors
with their assertion lines retained. A zero active-slideshow count returns
`InvalidState` before division. Other unusual integer behavior is retained
and covered by the comparison, rather than silently replaced with a different
navigation policy.

## Evidence

**3,097 deterministic original-instruction cases** match the reconstructed
widget fields, dirty flag, timers, assertion boundaries and ordered actions.
They include all 512 enabled-widget combinations with both encoder directions,
count/selection boundaries, extreme deltas, invalid character selections,
timer wrap, all duration increment transitions and byte-overflow cases in
combined mode. Source-only tests check required services and the division guard.

```sh
python3 tests/run_head_encoder_emulation.py \
  --project <byte-verified-z119-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> \
  --output build/head-encoder-original
python3 tools/validate_head_encoder.py \
  --original build/head-encoder-original --write-evidence
python3 -m unittest discover -s tests -p test_head_encoder.py
```

The fixture/report binds the image, executed function ranges, generator and
validated source. It does not establish complete ScreenLibrary behavior,
concurrent delivery, a linked firmware image or physical display operation.
