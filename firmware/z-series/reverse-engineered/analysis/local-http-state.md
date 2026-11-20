# Complete local `/state` response

`local_http_state.hpp` and `local_http_state.cpp` reconstruct the full document
selected by original `0x4011ae84` in Z119. The reference image hash is
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.
The implementation supplies a concrete snapshot adapter for the recovered
heater, sensor, MSP and Runtime types. The Wi-Fi owner supplies its signed
RSSI observation.

## Field order and meaning

| JSON field | Original CoreState offset/source | Output |
| --- | --- | --- |
| `ThermoBlockTemp` | `+0x548` | HEX temperature |
| `WortTemp` | `+0x558` | Wort temperature |
| `AmbientTemp` | `+0x578` | Ambient temperature |
| `DrainTemp` | `+0x568` | Drain temperature |
| `ValvePosition` | `+0x2e3` | Unsigned MSP valve byte |
| `KegPumpOn` | `+0x2e1` bit 4 | Integer 0 or 1 |
| `DrainPumpOn` | `+0x2e1` bit 5 | Integer 0 or 1 |
| `PauseReason` | `+0x2fc` | Runtime pause reason, original valid range 0..17 |
| `rssi` | `+0x80c` | Signed Wi-Fi signal observation |
| `TargetTemp` | `+0x2b0` | Heater target |
| `StepName` | `+0x304` | Borrowed current step string, or null |
| `SecondsEllapsed` | `+0x368` | Unsigned Runtime elapsed time; original spelling retained |
| `SecondsRemaining` | `+0x3a0` | Unsigned Runtime total remaining time |
| `UsePicoPakMode` | `+0x3b8` bit 0 | Integer 0 or 1 |
| `Location` | Valve position and PicoPak flag | Original logical location mapping |

Valve positions 2, 4, 5, 7, 8 and 9 map to locations 1, 2, 3, 4, 5 and 6.
Position 6 maps to 3 in PicoPak mode and 4 otherwise; other positions map to 0.

## Exact serialization

The original fifteen-field document fits its 1,024-byte JSON pool. The owned
writer emits this fixed schema directly, avoiding a runtime tree allocation.
Its numeric normalization, rounding and string escaping are recovered from
the original serializer, rather than delegated to the host platform's JSON
formatting defaults.

The double formatter preserves the original nine-digit precision and
scientific-notation thresholds, including signed zero, subnormals, large
finite values and null output for NaN/infinity. The original string writer
escapes quote, backslash, backspace, form feed, LF, CR and TAB, while passing
other non-NUL bytes through. The response preserves that behavior, including
raw control bytes; it does not claim standards-compliant normalization of
arbitrary input strings.

The original bounded writer reserves one byte for NUL. `/state` clears the
2,048-byte server buffer, freezes it, writes at most 2,047 bytes, sends a 200
header with the actual emitted length and `application/json`, then writes the
body. A deliberately oversized borrowed step name reproduces original
truncation, which can truncate JSON. Normal Runtime step-name storage is much
smaller. Invalid pause reasons return an error instead of the OEM assertion.

## Validation

All **80 scenarios** match complete original response bytes and server buffer
state. The Ghidra harness executes the full field-selection body, original
JSON member allocation/setters and original serialization helpers. It does
not substitute a host JSON library for the original output.

Cases cover every logical valve branch, both PicoPak modes, all eighteen valid
pause reasons, pump-bit combinations, signed/unsigned limits, rounding and
scientific-notation boundaries, subnormals, NaN/infinity, null strings,
escaping, UTF-8 bytes and output truncation. Host tests also verify the
concrete control-state adapter, output bounds and response-service failures.

Scalar compiler arithmetic, ROM memory/string routines, zeroed single-thread
C++ static guards, the separately verified header formatter and final socket
write are explicit execution boundaries. No physical network or machine was
tested. Source, instruction and fixture hashes are recorded in
`local-http-state-validation.json`; the target report records pinned Xtensa
GCC 5.2 compilation, with no complete application link.

```sh
python3 -m unittest discover -s tests -p test_local_http_state.py -v
python3 tools/validate_local_http_state.py --prepare-input /tmp/z-state-input.json
analyzeHeadless <project-directory> firmware \
  -process zseries_0_0_119.bin -noanalysis -readOnly \
  -scriptPath <this-directory>/tools/ghidra \
  -postScript EmulateLocalHttpState.java /tmp/z-state-input.json /tmp/z-state-original.jsonl
python3 tools/validate_local_http_state.py \
  --original-jsonl /tmp/z-state-original.jsonl \
  --export <preserved-instruction-export-directory> --write-evidence
```
