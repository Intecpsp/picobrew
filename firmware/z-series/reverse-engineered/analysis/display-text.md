# Z119 fonts, text context and UTF-8 rendering

The five original font descriptors and all **94,403 packed pixel bytes** are
recovered in `display_fonts.cpp`. `tools/extract_display_fonts.py --check`
verifies the exact source generation against the original, hash-checked
image. The font names describe their glyph dimensions; vendor typeface names
have not been inferred.

| Descriptor | Glyph size | Bitmap size | Data bytes |
| --- | --- | --- | --- |
| `0x3f42b31c` | 9 × 11 | 2,025 × 11 | 11,143 |
| `0x3f42ded4` | 17 × 20 | 3,825 × 20 | 38,260 |
| `0x3f437478` | 14 × 16 | 3,150 × 16 | 25,200 |
| `0x3f43d718` | 8 × 16 | 1,800 × 16 | 14,400 |
| `0x3f440f88` | 6 × 8 | 1,350 × 8 | 5,400 |

Each font contains 225 glyphs: codepoints 32–255 and the fallback glyph
selected as codepoint 256. The default Head constructor selects 6 × 8.
Source descriptors retain the observed row padding for odd bitmap widths.
The framebuffer is row-major packed grayscale, with even X in the high
nibble. A native rendering of `PicoBrew Z - test` is upright when row zero is
displayed at the top; physical display orientation is not asserted by that
software preview.

## Reconstructed behavior

`display_text.cpp` provides the original font/context operations, UTF-8
counter/decoder, glyph renderer and row/alignment renderer:

- `0x400d1ae4` changes the font and line spacing; `0x400d1aa8` changes the
  text rectangle. Columns divide its width by glyph width; rows divide its
  height by glyph height plus line spacing.
- `0x400d703c` counts bytes that are not UTF-8 continuation bytes.
  `0x400d70f8`, `0x400de2d8`, `0x400de30c` and `0x400de378` initialize and
  advance the decoder. Original malformed-sequence behavior is explicit.
- `0x400e7970` draws glyphs using the recovered packed bitmap copier.
  TAB and LF become spaces; codepoints outside 32–255 use the fallback.
- `0x400e7bdc` selects character count, row, inversion, alignment and X/Y
  offsets. Nonzero X offset bypasses normal column truncation and alignment.

The baseline preserves the original byte-based truncation of overlong UTF-8
labels: a character-count difference advances the byte pointer. An invalid
leading byte resets the decoder to the empty string and returns the fallback
sentinel. A malformed continuation is consumed and contributes zero bits;
the reconstructed code does not replace this with a different Unicode
algorithm while claiming original equivalence.

The original asserts on truncated and overlong sequences and on empty text
passed to the row renderer. The typed source returns explicit errors while
preserving the effects before that point. Null/unterminated input, excessive
byte capacity, invalid font/region and a crop beyond the source terminator
also have bounded failure paths. These are owned repairs, not successful
no-ops or claims that the original handled invalid memory safely.

## Validation

All **503 original/native scenarios** match full framebuffer hashes, text
metrics, character counts, decoded codepoints and decoder positions. The
suite includes every glyph of the default font, representative glyphs from
all four other fonts, both pixel alignments, inversion, ASCII and multibyte
labels, clipping, row/offset/spacing changes, and malformed sequences.
**27 cases** explicitly compare original assertion boundaries with the
typed error result and effects before failure.

The original executor reads font pixels directly from preserved DROM. The
native source uses the generated owned arrays. The executed function bytes,
input, fixture, source, and harness hashes are recorded in
`display-text-validation.json`. Ghidra adapts register windows; libc,
diagnostic and assertion calls are explicit boundaries. No machine, GPIO or
physical display is involved. This does not establish the remaining Head
screens, animation or the complete linked Z application.

```sh
python3 tools/extract_display_fonts.py --check
python3 tools/validate_display_text.py --prepare-input build/display-text-input.json
# Run EmulateDisplayText.java with -noanalysis -readOnly in the preserved project.
python3 tools/validate_display_text.py --original-jsonl build/display-text-original.jsonl \
  --export <preserved-callback-export> --write-evidence
python3 -m unittest discover -s tests -p test_display_text.py
```
