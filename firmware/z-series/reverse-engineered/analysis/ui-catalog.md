# Z119 localized UI catalog and bitmap assets

`ui_catalog.hpp/.cpp` supplies the original text and non-font bitmaps to the
reconstructed display code. It contains **405 entries in 18 text categories**,
each with English, French and Spanish strings, **102 screen bodies**, and
**16 packed grayscale bitmap descriptors** containing 3,183 pixel bytes.
The recovered catalog deduplicates its 550 distinct source string addresses.
Original spelling, translations, control characters and pixel data are retained.

The source generator reads the preserved 0.0.119 image directly. It verifies
the complete image hash, every mapped data range and bitmap geometry. Screen
text is decoded from the original switch jump table at `0x3f41f0c0` and the
reviewed `L32R`, language-index, load, store and return instructions. No
decompiler output or OEM image is needed when the generated source runs.
`ui-assets.json` records the exact table addresses, descriptor and pixel
hashes, screen instruction hashes and string addresses.

## Lookup behavior and limits

The 18 text accessors retain their original item bounds, invalid-item label
and fallback to English for an invalid language. Numeric `UiTextCategory`
values are an owned C++ API, not an original protocol.

The original screen getter at `0x400d9a2c` leaves the caller's output pointer
unchanged for `ScreenId::None` and unknown screen IDs. `ui_screen_text` retains
that behavior and returns `Unavailable`. A valid screen with an invalid
language originally reads beyond its three-entry table. The recovered
interface instead returns `InvalidLanguage` and leaves the output unchanged;
this is an explicit bounds repair, not an OEM-equivalence claim.

The assets include large and small logos, selection indicators, time
separators and diagnostic images. Names describe the recovered pixels and
their observed uses; vendor asset names were not recovered. Fonts remain in
the independently verified display-font module.

## Verification

`EmulateUiCatalog.java` executes the 18 original text accessors and the screen
getter in a byte-verified, read-only Ghidra project. The retained fixture
contains **2,706 cases**, including every valid entry in all three languages,
invalid item and language boundaries, every valid screen and unknown screen
IDs. It preserves exact returned UTF-8 bytes and unchanged-pointer behavior.
Diagnostic output, basename lookup and diagnostic clock reads are explicit
execution boundaries; native catalog functions do not reproduce diagnostics.

`validate_ui_catalog.py` checks fixture identity, generator hashes, original
instruction ranges, regenerated source and all native lookup results before
accepting the evidence. Native tests also compare every bitmap descriptor and
pixel byte to the original image, and check the added language guard.

```sh
python3 tools/generate_ui_assets.py --check
python3 tests/run_ui_catalog_emulation.py \
  --project <byte-verified-z119-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> \
  --output build/ui-catalog-original
python3 tools/validate_ui_catalog.py \
  --original build/ui-catalog-original --write-evidence
python3 -m unittest discover -s tests -p test_ui_catalog.py
```

The normal `make verify` suite replays the retained originals. The existing
ESP32 object builder also compiles `ui_catalog.cpp` with the pinned historical
compiler. This catalog does not implement ScreenLibrary layouts, menu/input
interaction, the remaining Head event handlers, or a complete ESP32 firmware
image. No hardware or physical display behavior is claimed.
