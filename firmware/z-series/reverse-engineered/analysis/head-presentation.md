# Hardware and transient Still display

`HeadHardwarePainter`, `HeadStillDisplay` and their shared `HeadPresentation` helpers recover the original Hardware/T1-monitor and transient PicoStill display bodies. Their drawing uses the existing recovered framebuffer, fonts, glyphs, UTF-8 decoder and bitmap operations. The portable interface accepts the same observed application state as the other Head painters; the Head screen owner supplies selection, update timing and shared diagnostics.

## Recovered behavior

The Hardware body at `0x400f0ab4` renders the T1 drop monitor when its extra argument is one. It shows the optional dropped-temperature warning, current T1 and maximum T1 in the chosen temperature unit. The other argument values produce an empty line collection. The original Graphics helper diagnoses that empty collection at `Graphics.h:0x5e`, calls the returning assertion helper and then leaves the framebuffer and dirty flag unchanged. The recovered body preserves that continuation.

The transient Still body is `0x400f1774`. The similarly named `0x401bda28` configures its rectangle and unit flag. The renderer translates the Still state title; state seven adds T2 and its localized instruction, while state eight asks the user to remove the pressure plug. Other states draw the title and outlined body. The outer `0x400f181c` tick supplies the strict deadline comparison and 500 ms interval, which belong to the composed Head lifecycle.

`HeadPresentation` recovers TextLines' eight 25-byte slots, overflow/truncation diagnostics, font selection, line distribution, vertical placement, centered text and title/body outlining. No host graphics library substitutes its draw operations. The helper explicitly requires the original assertion/log services when those diagnostic paths are reached.

## Verification and limits

The preserved Z119 instructions execute both renderer bodies and their Graphics, TextLines, display, UTF-8, font and bitmap callees in 71 cases. All 4,096 framebuffer bytes, dirty flag, last assertion line, global assertion counter and modeled clock match the native implementation. The replay executes 42 original functions and four calls through the actual returning assertion helper, including its counter increment and 200 ms delay. Coverage includes both unit modes, all three translations, Still states zero through eight, T1 warning state, the empty Hardware body and multiple rectangles.

The original execution boundary models constructor fields, reviewed ROM primitives, fixed-point `snprintf`/Xtensa `vsnprintf`, software floating-point helpers, clock, diagnostic output and delay. It does not replace any drawing callee. These are deterministic bounded comparisons, not an emulation claim for the whole ESP32 or a hardware qualification. The fixture and validation report pin their source/input hashes; fixture files use deterministic gzip.

Three native tests verify the complete original comparison, the empty Hardware body's returned assertion state and unchanged framebuffer, and evidence hashes. The pinned ESP32 compiler built all 105 recovered modules/adapters plus nine Ethernet2 dependency objects. `head-presentation-esp32-build.json` records the three owned object results; this compilation is not a complete firmware link.

Reproduce the portable checks from this directory:

```sh
python tools/validate_head_presentation.py --write-report
python -m unittest discover -s tests -p test_head_presentation.py -v
```

`tests/ghidra/EmulateHeadPresentation.java` and `tools/ghidra/DisplayExecution.java` reproduce the original instruction execution with the recorded input and field-map JSON. The original firmware image and its unmodified font/bitmap assets remain preserved.
