# Display console and demonstrations

All 20 original Display console callbacks and the full Display, TextModifier,
and Graphics demonstrations are implemented by `DisplayConsole`. The caller
binds the same display buffers, persistent text context, and dirty flag used
by the live Head application. GPIO/SPI, delay, diagnostics and test-scope
services are explicit application bindings.

The 79 original-instruction comparisons include the original category/result
renderer used by the aggregate console tests. They
compare the complete final framebuffer, transfer buffer, every transmitted
frame, ordered GPIO/SPI/delay/diagnostic/test-scope effects, ruler coordinates,
toggle/dirty state, and persistent text context. The full Display test
transmits 659 matching frames; Graphics transmits 38. Coverage includes all
five fonts, clipped coordinates, signed 32-bit console arguments and the
empty-text assertion. That case executes the real assertion helper, its
counter update and 200 ms delay, then completes the original caller.

The category/result renderer clears the framebuffer, draws a single centered
line with the fixed 6×8 font and zero line spacing, restores the persistent
text context, and flushes the display. Thirty additional comparisons cover
every previous font, both dirty-flag states, long and UTF-8 messages, and
empty/null messages. Empty/null input runs the original `Graphics.h` assertion
and still flushes the cleared screen; it does not set the dirty flag.

The fixture records explicit boundaries for ROM primitives, reviewed bounded
formatters and `atoi`, SDK transports, prescribed clocks, and the Xtensa
windowed calling convention. TestScope construction/destruction is captured
as an ordered effect; its diagnostic renderer is an application binding.
The evidence does not claim full CPU emulation or physical display testing.

`validate_display_console.py` verifies the preserved OEM image, exact input
case bytes and all executed function ranges before comparing native results.
The deterministic gzip fixture and source/generator hashes are checked by
`test_display_console.py`.
