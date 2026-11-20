# Z119 concrete Head widget painters

`head_widgets.hpp/cpp` implement eight original painters over the recovered
128 by 64, four-bit framebuffer. The source draws actual OEM glyphs and bitmap
pixels and uses the geometry and constructor defaults recovered from the
original UI manager.

| Painter | Original entry | Retained behavior |
| --- | --- | --- |
| Title | `0x400f8424` | Sixteen-byte owned title, thirteen-byte ellipsis prefix, centered 9x11 font, optional underline and 12/14-pixel consumed height |
| Progress | `0x400e9ed4` | Original bar margins, inclusive fill endpoint and bounded percentage formatting, including values above 100 |
| Temperature | `0x400e8aac` | Centered 14x16 value, degree glyph and C/F suffix |
| Duration | `0x400eb49c` / `0x400eb268` | Original HR/MIN label positions and 17x20 time digits; returning geometry assertions |
| Strength | `0x400e9cac` | Ten original segments, fixed frame coordinates and localized endpoint labels |
| Waiting | `0x400e8b44` | One or two centered lines and the four-frame ASCII spinner |
| Valve | `0x400ee760` | Exact location label, four port boxes, drain fill and invalid-location continuation |
| Text block | `0x400e7ddc` | Original alignment, padding, UTF-8 prefix/ellipsis and first-empty-line suppression |

The implementation preserves observed oddities: zero progress still fills an
inclusive endpoint, a full bar can extend one pixel beyond its outline, and an
empty first line suppresses the entire text block. A later empty line invokes
the original returning assertion before continuing the remaining lines.

## Evidence and execution boundary

`analysis/head-widgets-validation.json` binds the source to **237 original
instruction executions**. Every case compares the SHA-256 of all 4096 framebuffer
bytes, dirty flag, assertion line/counter and title height. Cases cover all eight
painters, languages, alternate rectangles, clipping, percentage overflow,
invalid valve locations, empty strings, long UTF-8 text, alignments and padding.
Original rendering continues through the actual rectangle, glyph, font and
bitmap helpers. The assertion body executes its counter update and 200 ms delay
and returns to the drawing code.

The oracle explicitly adapts Xtensa windowed calls and `MOVSP` stack movement.
Without the stack movement, Ghidra's zero synthetic WindowStart would make the
long-text temporary buffer overwrite live locals. This is an ABI adaptation;
it does not emulate CPU register-window spill/underflow exceptions. ROM memory
and string operations, bounded integer/character formatting, prescribed clock,
diagnostic output/delay and compiler arithmetic remain identified boundaries.
Every earlier comparison using the changed helper is rerun separately.

The bounded native UTF-8 decoder deliberately rejects malformed text, including
a title byte prefix that splits a multibyte character. The bounded rectangle
interface also rejects invalid intersections produced by undersized duration
regions after the original geometry assertion. Those rejection cases
are outside the valid-text parity evidence. The painters alone do not provide
screen ownership, dynamic slideshow/runtime views, complete menu animation,
physical display transfers or a complete firmware application.

```
python3 tests/run_head_widgets_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_widgets.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_widgets.py -v
```
