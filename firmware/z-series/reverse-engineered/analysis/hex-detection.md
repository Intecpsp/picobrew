# Z119 heat-exchanger sensor detection

`include/zseries/hex_detection.hpp` and `src/hex_detection.cpp` reconstruct
the decision logic at original function `0x400dd9f4` in Z 0.0.119. The image
identity is recorded in `hex-detection-validation.json`. This is a pure
reconstruction of the OEM decision and sensor conversion sequence; it does
not run an ADC transaction or operate a heater.

## Recovered inputs and decision order

The original function requests ADC acquisition, then reads five calibrated
Celsius values through `0x400dd770` in this exact order:

| Reading | Channel enum / value | Sensor type / value |
| --- | --- | --- |
| Ambient | `Ambient` / 3 | `MURATA_NCU18` / 2 |
| Wort | `Wort` / 1 | `SENSTECH_10-364&5` / 1 |
| Drain | `Drain` / 2 | `SENSTECH_10-364&5` / 1 |
| First heat-exchanger candidate | `Hex` / 0 | `SENSTECH_10-366` / 0 |
| Second heat-exchanger candidate | `Hex` / 0 | `AET_14015` / 3 |

It initially writes sensor type **4** as an invalid sentinel. It returns a
`HexDetectError` in Xtensa register `a2`; Ghidra's inferred `void` signature
misses that return value, but the instructions retain it across the final
diagnostic call. The owned API preserves both outputs.

Errors are selected in the following priority order:

| Code | Original enum name | Condition |
| --- | --- | --- |
| 1 | `AmbientAboveMax` | Ambient > 35 °C |
| 2 | `WortAboveMax` | Wort > 35 °C |
| 3 | `DrainAboveMax` | Drain > 35 °C |
| 4 | `AmbientWortDelta` | `abs(trunc(ambient − wort)) > 5` |
| 5 | `WortDrainDelta` | `abs(trunc(wort − drain)) > 5` |
| 6 | `DrainAmbientDelta` | `abs(trunc(drain − ambient)) > 5` |
| 7 | `BothHexInvalid` | Neither candidate matches a reference |
| 8 | `BothHexValid` | Both candidates match a reference |
| 0 | `None` | Exactly one candidate matches; output its sensor type |

A candidate matches if its integer-truncated absolute difference is **less
than five** from **any** of Ambient, Wort, or Drain. The algorithm does not
require matching all three. The two comparison boundaries are distinct:
reference deltas of 5.999 °C pass the `> 5` integer test, while a candidate
delta of exactly 5.0 °C fails the `< 5` test. The reconstruction retains this
behavior rather than silently replacing it with floating-point tolerances.

The ADC wrapper first validates all four samples, then performs the five
conversions above using the reconstructed calibration module. The pure
Celsius API rejects nonfinite values or a span that could make the original
integer conversion undefined. Rejection leaves the caller's result
unchanged. These two explicit adapter boundaries replace assertion/undefined
input behavior; they do not add an OEM error code.

## Original-instruction checks

`tools/ghidra/EmulateHexDetection.java` executes the original 470-instruction
function body, starting after `ENTRY` and ending before `RETW`. ADC transport
and calibrated Celsius results are explicit input boundaries. The harness
checks the exact original channel/type arguments for each of the five
conversion calls. Compiler arithmetic/comparison, clocks and diagnostic
logging are also explicit boundaries; an unexpected call or original
assertion fails the run. No Xtensa hardware-loop instruction occurs here.

The recorded run contains **8,439 distinct cases**, including a Cartesian
boundary matrix, single-channel edge variations and a deterministic set of
additional values. Every result and output sensor matched the compiled owned
source exactly. All nine OEM result codes were reached. The complete CSV and
its provenance are reproducible ignored build artifacts; the committed
report stores their hashes and the portable fixture retains **119** cases.

```sh
python3 tools/validate_hex_detection.py --prepare-input build/hex-inputs.csv
JAVA_HOME=/path/to/jdk-25 /path/to/ghidra/support/analyzeHeadless \
  /path/to/z119-project firmware -process zseries_0_0_119.bin \
  -noanalysis -readOnly -scriptPath "$PWD/tools/ghidra" \
  -postScript EmulateHexDetection.java "$PWD/build/hex-inputs.csv" \
  "$PWD/build/hex-original.csv" -max-cpu 1
python3 tools/validate_hex_detection.py --original-csv build/hex-original.csv \
  --export /path/to/z119-preserved-export --write-evidence
```

This establishes the recovered branch and conversion-selection behavior
within the stated execution boundaries. It does not establish complete CPU,
I2C, physical sensor, machine identity, heating or brewing behavior.
