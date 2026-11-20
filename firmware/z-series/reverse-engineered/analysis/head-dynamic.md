# Z119 recurring runtime, pause and level displays

`HeadDynamicPainter` implements RuntimeState (`0x400f96f4`), RuntimeInfo
(`0x400e89c4`), Pause (`0x40102788`) and Level (`0x400e905c`). It consumes owned
Core/runtime/sensor snapshots and paints through the recovered fonts and
framebuffer functions. The shared duration and instruction formatters recover
`0x400de56c`, `0x400de648` and `0x400de7b0` for both Head and console callers.

All **196 original-instruction cases match**. The fifty-eight instruction
cases cover every opcode and exact argument formatting. The remaining cases
compare complete 4096-byte framebuffer hashes, dirty state and assertion
line/counter. Coverage includes metric/imperial readings, MSP valve/pump/fan
state, runtime names, all tested pause reasons, clipped menu geometry, error
messages, two languages and clamped level positions. Pause assertions execute
the original log/counter/delay/return sequence and continue drawing.

The reference executes original drawing, localization, UTF-8 measurement and
formatting control flow. Constructor/snapshot fields, reviewed fixed-format
`snprintf`, arithmetic, ROM primitives, prescribed clock/log/delay and windowed
calls are explicit boundaries. Level uses the containing display's persistent text context. Its fixtures cover
the default 6x8 font with zero spacing and all five fonts with nonzero spacing. Invalid instruction indices that would
read beyond the original tables return a bounded native error.

The source compiles with the pinned ESP32 GCC 5.2 toolchain. Timer registration,
button delivery, hardware display transport, installation and complete firmware
linking remain separate evidence; these renderer comparisons do not establish
physical front-panel operation.

```
python3 tests/run_head_dynamic_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_dynamic.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_dynamic.py -v
```
