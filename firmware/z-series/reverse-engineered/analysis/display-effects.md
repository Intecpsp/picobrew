# Z119 display effects

`display_effects.hpp/.cpp` reconstructs the original gradients, caret geometry,
timed bitmap animation and slide effects. It uses the recovered packed-pixel
display implementation and real SPI/GPIO adapter; it does not substitute a
desktop graphics library for device rendering.

| Original entry | Recovered operation |
| --- | --- |
| `0x400e7150` | Vertical gradient that darkens existing pixels |
| `0x400e7274` | Horizontal gradient with the original two direction/origin flags |
| `0x400e93f8` | Four caret directions and the original default diagonal |
| `0x400e7904` | Animation timing, repetition and dirty-frame flushing |
| `0x400e7a7c` | Slide a bitmap toward a destination |
| `0x400e7b2c` | Compute the aligned destination and slide |

The gradients retain unsigned byte arithmetic, inclusive clipping and the
original zero-offset tail sentinel. That sentinel skips a one-pixel span at
an even X coordinate. They apply a minimum to the existing grayscale value;
they are not opaque fills. The source deliberately preserves this behavior in
the OEM reconstruction.

An animation advances only when `deadline < now`, draws the **previous** frame
index after advancing, and skips that draw when the finite repeat count is
finished. A pending dirty flag is processed afterward and can cause a second
flush during the same tick. Deadlines use the original unsigned arithmetic,
including its rollover behavior.

A slide draws and flushes each intermediate position, then delays and advances.
It does not draw the final target position. The original can loop indefinitely
when its positive step cannot reach the target. The reconstructed API rejects
nonpositive steps, invalid directions, movement away from the destination,
nondivisible distances, and more than 4,096 intermediate frames. These are
explicit bounded-input repairs, not claims of equivalent behavior for the
original infinite loops. Caret heights above 4,096 are likewise rejected.

## Verification

`tools/validate_display_effects.py` generates deterministic inputs and compares
the recovered C++ with the original Xtensa instructions executed by
`tools/ghidra/EmulateDisplayEffects.java`. The comparison includes the complete
logical framebuffer, reversed transfer buffer, serialized GPIO/SPI traffic
(including every transferred pixel byte), delays, animation counters and
deadlines. `analysis/display-effects-validation.json` records exact hashes,
function coverage and execution boundaries; the native test suite replays
the retained original results.

The instruction harness adapts windowed calls and the previously reviewed
4,096-byte reversal loop. SPI/GPIO, delay and libc operations are explicit
boundaries. This establishes application behavior for the recorded cases;
it does not constitute full CPU, RTOS or physical-display validation.
