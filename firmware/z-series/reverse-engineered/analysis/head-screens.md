# Z119 owned screen composition

`HeadScreens` owns menu payloads, paragraph lines, title/body buffers and the
renderers that operate on them. It shares the actual `HeadEncoderState` and
`UiScreenTickState` used by input and periodic dispatch. Button callbacks copy
their selected payload into an owned Head event; clearing or rebuilding the
menu cannot change an event that has already been posted.

The recovered composition is the original `UiManagement` reset (`0x400ef2a4`)
and screen selector (`0x40102d30`), with actual fonts, graphics, title/body
placement and menu/paragraph painters. Supported layouts in this evidence are
registration, scroll, character/quantity editors, temperature, duration,
strength, valve, waiting, progress, menu, icon/menu, paragraph/menu, combined
scroll/menu, body text, RuntimeState, RuntimeInfo, Pause and Level.
The home (`0x40103858`), instruction (`0x40104238`), waiting (`0x40103b1c`) and
registration (`0x40103aa4`) presentation helpers are concrete implementations.
The original UTF-8 word wrapper (`0x400e388c`, `0x400e35f0`) is also recovered,
including its last-line ellipsis and long-word continuation behavior.

The **335 original-instruction comparisons** match every 4096-byte framebuffer
hash, all 46 captured widget/state fields, menu commands/labels and paragraph
contents. Wrapping coverage includes every distinct original catalog screen
string as well as whitespace, long words and accented text. Invalid strength
and valve values execute the real returning assertion body, with its counter
increment and 200-ms delay, before comparing the resulting display/state.
The waiting count is loaded with `L16UI`: 65535 remains unsigned despite the
decompiler's inferred signed-short type.

An additional integration test exercises the real encoder and periodic
dispatchers against these owned painters. It checks selected menu events,
recipe payload lifetime, edited temperature submission, waiting animation
deadline equality, combined paragraph movement and caret scheduling. The
current source also compiles with the pinned ESP32 GCC 5.2 toolchain.

## Explicit execution limits

The temperature composition exposed a Ghidra 12.2 Xtensa `TRUNC.S` p-code
defect: its final multiply interprets the integer power-of-two scale as a raw
float. `DisplayExecution` now handles this instruction with an explicit finite
signed conversion using the mathematical power-of-two scale. Nonfinite values,
overflow and processor exceptions remain outside that boundary. This is
recorded alongside the windowed CALL8/MOVSP adaptation, ROM memory/string
operations, compiler arithmetic and prescribed clock/log/delay.

All **4,366 cases** in the eleven earlier reports that consume
`DisplayExecution` were executed again with this correction before their
generator hashes were refreshed. Their compared results remained unchanged.
The original machine code, imported image and pinned Ghidra installation were
not modified.

Hardware, Still and slideshow layouts remain explicit `UnsupportedLayout`
results in this composition checkpoint. Their painters and the full Head
application lifecycle need separate integration and evidence. Malformed or byte-truncated UTF-8,
invalid rectangles and inputs exceeding owned capacities are explicit bounded
errors. Neither this target-object build nor the framebuffer comparisons
establish a complete firmware image or physical display operation.

```
python3 tests/run_head_screens_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_screens.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_screens.py -v
```

## Connected input and recurring screens

The screen owner now connects the real character and quantity editors,
RuntimeState, RuntimeInfo, Pause and Level painters. Full original composition
comparisons include input fields, quantities, timer state, both unit systems,
two languages, titled/untitled geometry and all 22 diagnostic paragraph examples.
`HeadScreens::progress` applies the original validated percentage update.
RuntimeInfo uses the 500 ms interval recovered from constructor `0x400f36b8`.
A containing display can bind its persistent text context; screen reset restores
full bounds while retaining font and spacing, as the OEM does.
