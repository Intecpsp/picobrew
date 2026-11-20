# Z119 scrolling paragraphs and footer controls

`head_paragraph.hpp/cpp` reconstruct paragraph setup (`0x400e9a2c`), text and
footer drawing (`0x400e9814`) and blinking navigation carets (`0x400e95bc`).
`HeadScrollData` owns 32 buffers of 64 bytes. The renderer shares the exact
`EncoderParagraph` selection state used by the existing encoder dispatcher.

The source preserves original line spacing, visible-line calculation,
remaining space for a bottom control, vertical centering, the rounded inverted
footer, and the low-gray up/down carets. A nonempty footer label reduces the
visible-line count even when its command is None; the footer itself is drawn
only when both the command and label are present. Empty text rows still occupy
their original line position. Blink calls alternate painting and clearing the
caret rectangles; the surrounding screen tick supplies their cadence.

`analysis/head-paragraph-validation.json` binds the source to **120 original
instruction executions** and a deterministic gzip fixture. Every full 4096-byte
framebuffer hash and all paragraph/caret rectangles, selected/visible/total
line counts, command/alignment fields, deadline, dirty and blink flags match.
Coverage includes full and titled-screen rectangles, all text alignments,
absent/active/commandless footer labels, empty and scrolling text lists,
reserved bottom space, UTF-8 labels and repeated blink calls.

The oracle executes actual text lookup, font, bitmap, rounded-selection and
caret bodies. ROM memory/string operations, prescribed clock, compiler
arithmetic and windowed ABI adaptation remain identified boundaries. Invalid
regions and malformed UTF-8, including a stored byte prefix that splits a
multibyte character, are explicit bounded errors outside these matching cases.
This module renders into the recovered framebuffer; it does not establish
physical display transfers or complete firmware execution.

```
python3 tests/run_head_paragraph_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_paragraph.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_paragraph.py -v
```
