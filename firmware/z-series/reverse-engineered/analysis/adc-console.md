# Complete MAX11613 console continuation

The production `max i`, `max read`, and `max auto` callbacks now preserve the
original returning-assertion and clamping behavior. A short I²C reply logs the
read-count error and leaves all four raw channels at -1; the information or
detection callback then continues through its assertions and conversions.
Negative byte reads execute the original assertion and continue decoding every
channel. Samples outside 15..4094 emit the original ADC clamp diagnostic before
the bounded thermistor calculation.

Detection computes all five candidate temperatures before printing its five
information rows. This ordering matters when several channels are invalid:
their assertions and clamp diagnostics precede the information rows. The
previous reconstruction interleaved those two parts and stopped on short reads.

`adc-console-validation.json` records **68 complete original callback cases**,
**12 executed original functions**, and **275 returning assertions**. The oracle
executes the actual `0x400db014` body, including its counter increment and 200 ms
delay. It compares every semantic log, request/read operation, final raw sample,
assertion counter and wrapping clock. The compressed fixture retains hashes of
the preserved image, generated inputs and every executed function range.

SDK Wire return values, clock and delay, reviewed ROM memory/format operations,
compiler arithmetic, and finite logarithms are explicit environmental
boundaries. This is not a physical ADC test. Out-of-range sensor-type indices
remain a deliberate rejection in owned code; the oracle uses the four actual
sensor types and does not claim parity with an out-of-bounds vendor access.

Generate cases with `tools/validate_adc_console.py --prepare-input INPUT.json`.
Copy `tests/ghidra/EmulateAdcConsole.java` and `SlideshowExecution.java` into an
otherwise empty script directory, then run the former against the byte-verified
Z119 Ghidra project in read-only/no-analysis mode with the input and output JSON
paths. Pass that output to `tools/validate_adc_console.py --original OUTPUT.json
--write-evidence`. The native comparison is also exercised by
`tests/test_adc_console.py`. The existing 297-command console replay was rerun
after this change and its provenance was refreshed.
