# Z119 quantity editor and formatter

`HeadQuantity` owns the original discrete range, selected units and editor
geometry while sharing the recovered `EncoderQuantity` consumer state. It
implements the volume, temperature, intensity and time painters and emits
owned quantity events on a button press.

The **242 original-instruction comparisons** match the native result: fifty-six
initializations compare complete framebuffer hashes and all selector metadata;
186 formatting cases compare exact text. Coverage includes metric/imperial
volume, mass, temperature, duration and intensity, conversion, range endpoints,
and out-of-range inputs. Original assertions log, increment the shared counter,
delay 200 ms, return, and continue into range clamping. The emulator executes
that helper and continuation. The converted secondary temperature is printed
with one decimal place; its fifth formatter argument is present in instructions
but absent in the decompiler's shortened call.

The original initialization, range selection, painter dispatch, shape/font
routines and unit formatter execute. Explicit boundaries cover constructor
fields, windowed calls, ROM primitives, compiler arithmetic, prescribed clock,
log/delay, and reviewed fixed-format `snprintf`. That formatting boundary
implements the encountered fixed-point, string and integer forms, including
half-even decimal rounding. Finite rounding and unsigned conversion reject
values outside their declared oracle bounds. This evidence does not cover
nonfinite values, impossible geometry or unsupported selector IDs.

The source compiles with the pinned ESP32 GCC 5.2 toolchain. Containing Head
screen/event integration is separately verified; neither object compilation
nor instruction replay demonstrates physical display operation or complete
firmware installation.

```
python3 tests/run_head_quantity_emulation.py --project <copied-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> --output <new-output>
python3 tools/validate_head_quantity.py --original <new-output> --write-evidence
PYTHONPATH=tests python3 -m unittest tests/test_head_quantity.py -v
```
