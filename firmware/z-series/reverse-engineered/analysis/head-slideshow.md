# Slideshow pages and lifecycle

`HeadSlideshow` owns the recovered twelve information-page entries, titles, per-page timers and cyclic timer. It shares `EncoderSlideshow` with the real Head encoder and draws through the recovered framebuffer/font/bitmap primitives. The Head screen owner supplies observed application state, a clock, its owned event receiver and the existing assertion/log/ScopeLogger services. Missing required callbacks return an explicit failure.

## Pages

The original `0x400fd970` renderer dispatches these concrete bodies:

| Type | Content | Original body |
| --- | --- | --- |
| 1 | Session name, UTF-8 truncation and ellipsis | `0x400f2054` |
| 2, 3 | Remaining and elapsed time with original fonts/separator bitmaps | `0x400e85f4` |
| 4, 5, 6 | Wort/target temperature, target divider and unit conversion | `0x400e8f8c` |
| 7 | Step, substep and relevant instruction countdown | `0x400f0d98` |
| 8 | Wort, HEX, drain and ambient temperatures | `0x400f0b80` |
| 9 | Pressure, altitude and adjusted boiling point | `0x400f0ca4` |
| 10 | Wort and HEX temperature rates | `0x400fd970` |
| 11 | Raw Still temperatures, pressure, wort and target | `0x400f1028` |
| 12 | Vapor pressure, temperature, trends and pressure stability | `0x400fd4e4` |
| 13 | Pot T1/T2, their trends/stability and temperature difference | `0x400fd680` |
| 14 | Still heat-loss reference and recorded boiling point | `0x400f1190` |
| 15 | Pressure/T1/T2 sample stability | `0x400fd878` |
| 16 | T1 temperature-drop monitor | `0x400f0e40` |
| 17 | Still connection-check pressure and temperature trends | `0x400f0f14` |
| 18 | Offline warning and current Wi-Fi state | `0x400fd970` |

The renderer retains the original distinctions between absolute temperature, temperature difference and temperature rate. In particular, Still heat-loss reference display and pressure scaling follow the actual instructions, and the vapor/checking trend threshold is exactly 0.6. Under-hour step countdowns omit the zero-hour prefix. Large time values retain the original bounded hour-string truncation. Invalid enum zero and the end sentinel log the original diagnostic; other out-of-range values follow the original empty switch continuation.

## Selection, updates and diagnostics

`0x400fe6bc` selects pages from program mode, network availability, offline/service settings, PID state, Still pump state and detail preference. `0x400fdb54` selects an entry, clears and outlines the screen, paints arrows/title, initializes its counter and starts the corresponding 500 ms or 1,000 ms timer. The optional cyclic timer uses 4,000 ms. Entry selection preserves negative/out-of-capacity diagnostic assertions and their continuation to entry zero.

`0x400fe960` updates active pages at inclusive deadlines and rebuilds the list when the observed PID, pump or network state changes. Its counters and deadlines retain unsigned 32-bit arithmetic, including the original countdown wrap through zero. Reset executes the recovered stop/clear behavior. The button delivers the command to the supplied receiver and resets; the composed Head receiver acknowledges a serviced full-mailbox drop so the original reset continuation is preserved.

The `0x400fea6c` diagnostic list retains its unconditional capacity assertion, then fills types one through eleven, selects the first page and starts cycling. The `0x400fe0b8` StabilityText diagnostic executes the four original comparisons and Warning logs between paired ScopeLogger calls. Text buffer truncation diagnostics preserve their original lines and spelling. The shared presentation helper's empty-text diagnostic now uses the exact `Display.h:0x3bf` expression.

## Evidence

All 328 original/native cases match, including all 4,096 framebuffer bytes, dirty flag, last assertion line, assertion counter, modeled clock, every entry's type/title/counter/timer, cyclic state and diagnostic/scope callback order. The replay executes 96 original functions and 19 calls through the actual assertion helper `0x400db014`, with its counter increment, 200 ms delay and normal return. Coverage includes every page, all translations, both unit modes, timing and numeric boundaries, list rebuilding, invalid selections, reset, the original diagnostic page list and StabilityText test.

The root constructor geometry and the initialization caller sequence are explicit fixture setup. Entry constructors and renderers execute their original instructions. Reviewed ROM/libc formatting, software floating point, clock, delay, stack high-water mark and task watchdog reseting are environmental boundaries. ScopeLogger bodies execute, while their platform text rendering is independently owned by the System console evidence. The higher-level HeadScreens reset preceding the slideshow diagnostic is delegated to its separate replay; the slideshow reset itself executes original instructions. No drawing callee is replaced with a native expected result. This is bounded original-instruction comparison, not a full ESP32/RTOS emulation or hardware qualification.

Five native tests cover the complete comparison, returned invalid-selection assertions, countdown wrap, required-service failure and evidence hashes. The pinned ESP32 build contains 106 recovered modules/adapters plus nine Ethernet2 objects. The owned object extraction is `head-slideshow-esp32-build.json`; source changes after that build were recompiled with the same target language/common flags and replaced in the archive. No complete firmware link, flash operation or hardware test is claimed here.

From this directory, reproduce the comparisons with:

```sh
python tools/validate_head_slideshow.py --write-report
python -m unittest discover -s tests -p test_head_slideshow.py -v
```

The deterministic gzip fixtures hold the inputs and original results. `tests/ghidra/EmulateHeadSlideshow.java` accepts the decompressed inputs, a JSON field map containing `words`, `doubles` and `floats` from `tests/head_slideshow_cases.py`, and an output path. Its private `SlideshowExecution.java` adds the reviewed 25-byte TextLines constructor loop to the existing display execution helper without changing shared helpers or their evidence hashes.
