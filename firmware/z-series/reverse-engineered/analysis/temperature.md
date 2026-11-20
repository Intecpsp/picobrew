# Z119 temperature and ADC reconstruction

This is a readable reconstruction from the unchanged `zseries_0_0_119.bin`,
SHA-256 `d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.
The original application identifies the relevant header as `MAX11613.h`.
The reconstructed pure calculations compile independently of the original
image, original RAM, Arduino, and a connected machine.

## Address traceability

| Original virtual address | Recovered behavior | Owned source |
| --- | --- | --- |
| `0x3f4287f0` | Four 40-byte thermistor parameter records | `src/temperature.cpp`, `Parameters` |
| `0x400d6ac0` | Raw ADC clamp and thermistor Kelvin calculation | `thermistor_kelvin` |
| `0x400dd770` | Raw/type guards followed by Kelvin minus 273.15 | `raw_adc_valid`, `thermistor_celsius` |
| `0x400dd908` | Four big-endian 12-bit ADC values; all invalid after a short transaction | `decode_max11613_reply` |
| `0x400dd708` | Big boiler uses sensor type 0; small boiler uses type 3 | `hex_sensor_for_boiler` |
| `0x3f428890` | Four sensor display names | `enums.hpp`, `TempSensorType` |

The original table is `type:uint32, padding:uint32, A:double, B:double,
C:double, series_resistance:double`. Each padding word is zero. The owned C++
record expresses those fields without depending on the vendor object layout.

| Type | Original label | A | B | C | Series resistance |
| --- | --- | --- | --- | --- | --- |
| 0 | `SENSTECH_10-366` | 0.0009111439983 | 0.0002472353473 | 2.121428624e-7 | 10000 Ω |
| 1 | `SENSTECH_10-364&5` | 0.001127286052 | 0.0002351430103 | 7.753973551e-8 | 10000 Ω |
| 2 | `MURATA_NCU18` | 0.000901747748 | 0.000248919031 | 2.043213857e-7 | 10000 Ω |
| 3 | `AET_14015` | 0.0006232719375 | 0.0002276660423 | 7.184906863e-8 | 10000 Ω |

Tests compare all sixteen binary64 coefficient values directly with their
original DROM bytes, not with a second decimal transcription.

The original calculation clamps raw ADC to 15..4094 inclusive, computes
`R = (4095 / raw - 1) * series_resistance`, and then uses
`1 / (A + B*ln(R) + C*ln(R)^3)` for Kelvin. The source preserves the original
multiply/add order. Celsius subtracts the original binary64 value 273.15.
The original caller asserts ADC validity before conversion; the lower-level
calculation nevertheless clamps values. Both facts remain visible in the API.

The ADC byte layer masks the top four bits of each big-endian pair, retains
all twelve sample bits, and orders channels Hex, Wort, Drain, Ambient. A
sample of 0 or 4095 is therefore a decodable ADC value but an invalid raw
temperature sample. Any reply length other than eight invalidates all four
channels. The original `Stream::read` assertion and actual I2C transaction
are outside this pure byte adapter. Rejecting a null buffer is an explicit
adapter check, not an inferred OEM assertion.

The boiler helper reconstructs the original valid-domain mapping. Its
rejection of Unknown or an invalid enum replaces the original assertion and
fallback path; this is documented API boundary handling. Boiler size does
not identify a Z1 or Z2 machine.

## Original-instruction validation

`tools/ghidra/EmulateTemperature.java` executes all 114 instructions in the
original temperature function as reached for 4 × 4096 input combinations.
It begins after the three-byte Xtensa `ENTRY` and stops at `RETW`, so it does
not claim to emulate register-window calls or a booted ESP32. The function
contains no Xtensa hardware-loop instruction. This matters because the
native Ghidra Xtensa model loses hardware-loop behavior in another Z routine;
an unreviewed decompiler or emulator result is not sufficient evidence.

Calls to the identified compiler binary64 arithmetic helpers and `log` are
explicit boundaries implemented with Java arithmetic/`Math.log`. Clock and
diagnostic logging are modeled separately. An unexpected call target or
undecoded instruction stops validation. The original calibration table is
read from the mapped image by the original instruction sequence.

The local validation covered **16,384 cases**, including ADC values 0..14
and 4095 that enter the original clamp diagnostics. All reconstructed native
C++ binary64 results matched the hooked original execution exactly in the
recorded environment. This is not proof of bit-identical execution of the
vendor's complete `libm`, physical sensor accuracy, or electrical behavior.
Portable tests allow 2e-12 K variation across host math libraries.

`temperature-validation.json` records scope, input and source hashes,
compiler flags, complete execution CSV hash, instruction metadata hash,
case counts, and comparison results. `tests/fixtures/temperature-original.json`
contains 72 representative original-execution results; the full CSV is an
ignored reproducible build artifact. Native tests also cover all 4096 ADC
codes, failure lengths, channel ordering, signed input extremes, and boiler
selection rejection.

To reproduce after the shared ESP32 segment import and Ghidra analysis:

```sh
JAVA_HOME=/path/to/jdk-25 /path/to/ghidra/support/analyzeHeadless \
  /path/to/z119-project firmware -process zseries_0_0_119.bin \
  -noanalysis -readOnly -scriptPath "$PWD/tools/ghidra" \
  -postScript EmulateTemperature.java "$PWD/build/temperature-original.csv" 4095 \
  -max-cpu 1
python3 tools/validate_temperature.py build/temperature-original.csv \
  --export /path/to/z119-preserved-export --write-evidence
make verify
```

The `libgcc` helper identifications are recorded in `math-symbols.json`.
`log` at `0x4013b288` is additionally identified by the pinned Arduino ESP32
1.0.1 `libm.a` wrapper: 103 normalized instructions, internal branch targets,
registers and immediates agree after accounting for linker relaxation. The
relocated external bindings and literal checks are recorded by the shared
SDK matching workflow. No library routine is renamed solely because its
output resembles a desired formula.

## Remaining integration work

The recovered heat-exchanger detection decision and input mapping are in
[`hex-detection.md`](hex-detection.md). ADC bus initialization/address/pins,
sensor acquisition scheduling, heater feedback, and thermal interlocks are
not implemented by this module. A native library build is not a full application
link and is not a flashable image. Physical installation and brewing remain
unverified.
