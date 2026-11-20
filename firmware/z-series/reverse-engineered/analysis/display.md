# Z119 display hardware, rectangles and packed pixels

`display.hpp` and `display.cpp` reconstruct the Z application's display
hardware, rectangle calculations, fills/outlines and bitmap copies. This is
the Z's SSD1325 path, independently recovered from image
`zseries_0_0_119.bin`; it does not use the S/Pro or C framebuffer representation.

## Recovered representation and calls

| Original evidence | Meaning |
| --- | --- |
| Head constructor `0x400ff018`, display at Head `+0x17f8` | SPI instance and chip-select 0, data/command 2, reset 4 |
| `0x3f4269fc` | Inclusive rectangle `(0, 0, 127, 63)` |
| Display `+0xb` | 4,096 bytes of packed 4-bit grayscale pixels; even X uses the high nibble |
| Display `+0x100b` | Separate 4,096-byte SPI transfer buffer |
| Display `+0x2068` | Framebuffer row stride, 64 bytes |
| `0x400fc9c4` | GPIO initialization and the 30 original command bytes, followed by information/console effects |
| `0x400d1b24` | Command transfer: literal DC 2 low, CS 0 low, SPI byte, CS 0 high |
| `0x400d1b6c` / `0x400d1b88` | Clear the logical buffer / reverse all bytes into the transfer buffer and send it |
| `0x400e880c` | Clipped fill or outline, including the original single-column quirk |
| `0x400e7460` | Clipped packed bitmap region copy, with optional inversion |
| `0x401bf500`–`0x401bf5f4`, `0x400e70cc` | Width, height, center, intersection, containment and translation |

The flush driver sends column range 0–63 and row range 0–63, raises DC,
lowers CS, reverses all 4,096 bytes, sends that transfer buffer, and raises CS.
The original `LOOP` at `0x400d1bd2` is essential: the decompiler omits its
back edge and falsely shows a single-byte copy. Reconstructed source uses
an explicit complete reversal.

Setup/flush read the object's pin fields, while the command method uses
literal pins 2 and 0. The production constructor selects those same pins.
Setup drives RESET high; no additional pulse or delay has been invented.
The real ESP32 adapter calls Arduino GPIO and `SPIClass` operations on the
System-owned bus. Its setup information and console registration callbacks
are required integration services. This module does not claim that the
remaining display console, text, animation or Head/UI is complete.

## Observable original quirks and bounded adapters

An outline is drawn around the clipped rectangle. A one-pixel-wide outline
at even X also modifies the preceding byte's low nibble on interior rows;
at X=0 this is the previous row's last pixel. The aligned, non-inverted
bitmap copy omits a one-pixel even-X span. Both behaviors are preserved in
the OEM baseline and covered by original-execution examples. They are not
silently repaired under a claim of equivalence.

The typed adapter checks callbacks before effects and checks bitmap byte
capacity, stride and coordinate arithmetic before reading or drawing.
Invalid descriptors return an explicit error instead of the original
assertion or out-of-bounds behavior. These owned failure paths have separate
negative tests and are not labeled original-equivalent.

## Comparison and limits

The original executor runs 18 original bodies across 1,010 deterministic
scenarios. The native source matches all complete logical-buffer hashes,
transfer-buffer hashes, SPI payload hashes, ordered GPIO/SPI/setup effects,
and rectangle results. Cases cover every command byte, reset enabled/absent,
both packed-pixel alignments, clipping, padded bitmap rows, inversion,
full-screen transfers, degenerate spans and signed rectangle coordinates.

`DisplayExecution.java` is isolated from the shared executor. It adapts
Xtensa register windows and the one reviewed 4,096-iteration display LOOP;
all loop-body loads/stores still execute original instruction semantics.
Unknown calls and other LOOP sites are rejected. SPI, GPIO, non-overlapping
`memcpy`, `memset`, diagnostics and setup information/console operations are
explicit boundaries. The recorded hashes identify the executor and source.

This is not full CPU/interrupt emulation or a physical display test. The
comparison does not establish electrical signaling, hardware identity,
power-on behavior, or the full Head/UI. No machine is contacted or flashed.

```sh
python3 tools/validate_display.py --prepare-input build/display-input.json
# Run EmulateDisplay.java with -noanalysis -readOnly in the preserved project.
# Pass absolute paths for the input JSON and output JSONL.
python3 tools/validate_display.py --original-jsonl build/display-original.jsonl \
  --export <preserved-callback-export> --write-evidence
python3 -m unittest discover -s tests -p test_display.py
```
