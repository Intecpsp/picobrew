# Z119 menu and scrolling label composition

`head_menu.hpp/cpp` supply actual title/body/menu composition through the
original `0x400f86c8` layout, `0x400eb14c` initialization, `0x400eb07c` drawing,
`0x400dfa7c` long-label bitmap generation and `0x400e79f8` timed scrolling.
They render the original rounded selection corners, arrow assets, inverted
selected labels, faded neighboring rows and top/bottom rules.

The original menu selects one, three or five rows from the requested maximum,
available height and item count. Six items are required before five rows are
selected. A leading Back entry is skipped when another item exists. A title
receives an underline for a one-row menu or when a body is present. The body
uses the remaining rectangle above the menu, preserving original alignment
and title-height accounting.

`HeadMenuData` owns 32 complete `HeadUiEvent` payloads and 128-byte label buffers.
Copies retain recipe/program data, independent of the originating callback.
`HeadMenuRenderer` references the same `EncoderMenu` as `HeadEncoder`, so a turn
changes the selection that is actually redrawn; there is no disconnected
renderer-only selection copy.

Long selected labels use the recovered 3840-byte backing bitmap. Initial
display sets the first deadline 25 ms ahead. The first scrolling draw repeats
position zero and then pauses for 800 ms; subsequent steps occur after 25 ms.
When the right edge reaches the full label width, the source offset resets
without drawing, and the pause begins again. Exact deadline equality does not
advance a frame.

## Validation

The deterministic gzip fixture and `analysis/head-menu-validation.json` cover
**139 completed original instruction executions**. Native results match every
full framebuffer hash, full marquee backing-buffer hash, menu/title/body
rectangle, row count, selection/offset, gradient state, scroll timing and
assertion count. Coverage includes zero maximum rows, all three menu sizes,
Back selection, alternate selected items, title/body composition, long ASCII
and UTF-8 labels, empty selected text and sequences crossing scroll deadlines.
Rendering, font copying, UTF-8 truncation, gradient/corner/arrow drawing and
assertion continuation all execute their original instruction bodies.

An empty menu is a bounded source rejection after the original `0x124` and
`0x126` diagnostics; the OEM then obtains an index of UINT_MAX and can read
before the item array. Oversized labels, malformed UTF-8 and invalid crops are
also explicit bounded errors, outside the matching-case claim. ROM primitives,
compiler arithmetic, prescribed clock/log/delay and adapted windowed ABI are
identified oracle boundaries. This evidence does not establish physical
display transfers or complete firmware execution.

```
python3 tests/run_head_menu_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_menu.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_menu.py -v
```
