# Z119 character picker

`HeadCharacter` recovers the circular character painter at `0x400edd28` and
button handler at `0x400fced0`, using the original font/bitmap assets and the
same `EncoderCharacter` state that the recovered rotation consumer updates.
It owns the editable text, length, capacity/truncation flag and response command.

The **226 original-instruction comparisons** match all 4096 framebuffer bytes
through SHA-256, selection/enabled state, dirty state, editable text and copied
events. Coverage includes decimal, hexadecimal/RFID and full text selections,
the action icons, all three languages, empty/full buffers, deletion, cancellation,
and RFID submission's exact fourteen-character gate. The OEM text-space action
bypasses the forty-two-character editing gate; the fixed sixty-four-byte buffer
still bounds storage. The implementation preserves that distinction.

Draw calls, rounded selection corners, gradients, font decoding, text-buffer
mutation and the button's internal state changes execute from original image
instructions. The oracle supplies constructor fields and explicit event sinks,
ROM primitives, compiler arithmetic, prescribed clock/log/delay and windowed
call adaptation. Layout initialization and the containing Head event lifecycle
are separate integration responsibilities. Invalid geometry or impossible
selection state returns an explicit native error.

The concrete source compiles with the pinned ESP32 GCC 5.2 toolchain. This
object compilation and instruction replay do not establish a complete firmware
image or physical front-panel validation.

```
python3 tests/run_head_character_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_character.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_character.py -v
```
