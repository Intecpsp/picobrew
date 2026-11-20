# Z119 LIS3DHTR and BMP280 reconstruction

The owned auxiliary-sensor module reconstructs the original I²C transactions,
axis smoothing, calibration loading and pressure compensation. It uses the
same `I2cTransport` and acquisition-clock adapters as the temperature path.
Core construction assigns both devices to I²C peripheral 0: LIS3DHTR at
`0x19` and BMP280 at `0x76`. No new sensor library or guessed hardware API is
introduced.

| Original address | Recovered body |
|---|---|
| `0x400d16b0` / `0x400d183c` | Identical register-reading helpers |
| `0x400de078` | LIS3DHTR setup |
| `0x400d1720` | LIS3DHTR read and axis smoothing |
| `0x400d1898` | BMP280 24-byte calibration read |
| `0x400de10c` | BMP280 setup |
| `0x400d91ac` | BMP280 raw read, compensation and boiling-point estimate |

## Bus behavior and state

Each register helper starts a transmission, writes `register | 0x80`, ends
with STOP, and requests the exact number of bytes with STOP. A nonzero
transmission result or a short/long response leaves the destination buffer
untouched. Individual `read()` results are narrowed to bytes, including an
unexpected `-1` becoming `255`. This reflects the original behavior; the owned
status result additionally makes bus errors visible to the application.

LIS3DHTR setup writes registers `0x1f`–`0x25` with
`00 47 00 00 88 00 00`, then reads identity register `0x0f` and enables polling
only for `0x33`. The tick reads six bytes at `0x28`, forming three signed
little-endian 16-bit values. Each axis is divided by **15987.0**, then blended
as 0.002 of the new value plus 0.998 of the prior smoothed value. The X addition
order differs from Y/Z in the original and is retained. When the read fails,
the original still smooths the previous raw values; it does not skip the tick.

BMP280 setup writes `0xf4 = 0x3f`, checks identity `0xd0` for `0x58`, and reads
24 calibration bytes at `0x88`. The first temperature and pressure coefficients
are unsigned 16-bit; the other ten are signed. Calibration is attempted even
when the identity read fails. Setup transmission results for register writes
are ignored in the original. Wrong identities cause a returning assertion
diagnostic. Production Core supplies the real diagnostic sink, which logs,
increments the assertion counter and delays 200 ms before setup continues.
The owned status flags expose the condition without inventing a successful
sensor detection or stopping later Core setup and tick services.

Pressure state holds signed 64-bit raw fields, a signed 32-bit `temperature_fine`
intermediate and the twelve calibration coefficients. The reconstruction
expresses the compensation with explicit 64-bit wrapping and arithmetic shifts
to avoid host C++ signed-overflow and negative-shift undefined behavior.
The original converts the final fixed-point result to **float**, multiplies
by `1/256` as float, and only then widens to double. A zero divisor produces
zero pressure. The timestamp is refreshed and the boiling estimate is set to
`(pressure_pa - 101325) * 0.00034724743 + 100`.

## Original pressure-byte defect

The original reads three bytes for each pressure/temperature raw value but
assembles them as:

```cpp
(byte[0] << 12) | (byte[1] << 4) | (byte[0] >> 4)
```

It ignores `byte[2]`. This is confirmed directly by the byte loads and shifts
at `0x400d91c3`–`0x400d91d5` and `0x400d91ed`–`0x400d91ff`, not inferred from
a questionable decompiler variable. `original_bmp280_raw` preserves it for
OEM reconstruction. A firmware correction must be a separate intentional
change; the tests vary the entire ignored-byte range to document this boundary.

Another retained behavior is that failed pressure/temperature reads reuse the
previous raw fields, recompute the reading and refresh its timestamp. The
owned return flags expose the failed transport; this reconstruction does not
silently convert that original timestamp into a freshness guarantee.

## Validation and reproduction

All **641 original-instruction cases** match the native implementation:
complete setup/read transactions, device state, raw fields, coefficient bytes,
compensation intermediate and timestamp. All **3,205 double comparisons** are
bit-identical in the recorded environment. The compressed portable fixture
retains every case, including 32 executions of the actual returning assertion
helper. Comparisons include exact diagnostic file, line and expression,
counter changes, the 200 ms delay, subsequent calibration reads and clock
samples. Separate tests cover the byte defect, failed-read behavior, missing
transport boundaries and Core's continuation after nonfatal sensor failures.

The Ghidra harness executes all seven original sensor bodies with nested
register reads and the real assertion helper at `0x400db014`. Compiler
arithmetic, Wire, clock, printf, basename and SDK delay are explicit boundaries.
The assertion helper itself is not replaced by a boundary. `MEMW` is an
explicit single-thread ordering boundary;
the fixture supplies a stack canary and checks the original comparison. This
does not simulate concurrent interrupts or validate physical sensors.

`auxiliary-math-symbols.json` records unique bounded libgcc matches for
`__divdi3` at `0x401c025c` and `__floatdisf` at `0x401c0120`, including object
hashes and relocation masks. These identify the arithmetic hooks without
claiming complete SDK equivalence.

```sh
python3 tools/verify_math_symbols.py --toolchain <pinned-toolchain> \
  --catalog analysis/auxiliary-math-symbols.json
python3 tools/validate_auxiliary_sensors.py --prepare-input build/auxiliary-input.json
# Run EmulateAuxiliarySensors.java with -noanalysis -readOnly in the preserved
# project; arguments: absolute input JSON and original-output JSONL paths.
python3 tools/validate_auxiliary_sensors.py --original-jsonl build/auxiliary-original.jsonl \
  --export <preserved-export> --write-evidence
python3 -m unittest discover -s tests -p test_auxiliary_sensors.py
```

The original binaries remain unchanged. The source requires no OEM bytes at
runtime. No flash image has been installed and no hardware was exercised.
