# Z119 sensor acquisition

`sensor_acquisition.hpp/.cpp` recovers the MAX11613 bus operations and the
temperature smoothing/publication path used by the main Core loop. It reuses
the independently checked calibration routines in `temperature.cpp`.

## Evidence and behavior

| Original address | Behavior |
|---|---|
| `0x40120a04` | Core constructor: MAX11613 at Core `+0x48b4`, address `0x34`, primary I²C bus, SensorsState pointer |
| `0x400fad30` | Configure channel sensor types, perform ADC startup transaction, clear raw readings, enable polling |
| `0x400dd68c` / `0x400dd708` | Assign a channel calibration / choose the Hex calibration from boiler type |
| `0x400dd908` | Request exactly eight bytes and decode four 12-bit readings |
| `0x40110990` | Update or expire the ten-sample noise smoother |
| `0x40110924` | Store a sample, update the ring/sum and calculate the average |
| `0x400ee1f0` | Check whether the ten-sample frame is full |
| `0x40120120` | Core tick: acquire, convert, smooth and publish valid channels |

The original setup writes `0x80` after beginning a transmission, waits 10 ms,
then calls `beginTransmission` again before writing `0x82, 0x07` and ending
with STOP. **There is no `endTransmission` after `0x80`.** The source preserves
this observed call sequence. Treating the first byte as a completed reset
transaction would change the original behavior.

The request path first sets all four raw channels to `-1`. Only an exact
eight-byte reply is decoded. Each pair is big endian and masked with `0xfff`.
A negative byte read triggers a returning OEM assertion. With the production
diagnostic sink bound, the adapter logs it, increments the diagnostic counter,
delays 200 ms and continues decoding all four channels. The masked result may
still be a valid raw value and therefore reach temperature publication. The
owned `ReadFailure` status records the event without stopping later Core
services. A short reply logs its expected/actual counts and leaves previously
published temperatures and timestamps untouched, allowing the existing
stale-input checks to observe the loss of data. Core continues after this
nonfatal result as well.

Hex uses calibration type 0 for Big and 3 for Small. Wort and Drain use type 1;
Ambient uses type 2. Raw values 15 through 4094 alone reach the conversion and
publication path. Neither boiler name establishes a Z1/Z2 model mapping.

The smoother is a ten-element ring and a running sum. Before ten samples it
divides by the number received; once full it removes the overwritten sample
before adding the new one. A gap **strictly greater than 2,000 ms** resets
the ring index, count, sum, average and result timestamp, but does not clear
old sample bytes. Elapsed time uses unsigned subtraction. A previous timestamp
of zero disables that particular gap test. Production diagnostics preserve the
additional clock sample and `NoiseSmoother.h:0x6e` warning before an expired
frame is reset.

Core publication uses the Core tick's `now` argument. The smoother obtains a
separate `millis()` value for each valid channel. The typed interface preserves
this distinction with `tick_ms` and `AcquisitionClock`. A zero Core timestamp
is asserted **after** smoothing each valid channel. The helper returns, and
that channel is still published with timestamp zero. Its 200 ms delay affects
the clock sampled for later channels.

## Validation

`acquisition-validation.json` records **340 original-instruction cases**,
including startup, short replies, random byte replies, ring wrap, timestamp
zero, rollover, and 2,000/2,001 ms gap boundaries. The **3,116 smoother updates**
match all integer fields and all **37,392 binary64 fields bit-for-bit**.
Portable tests retain 164 scenarios. GPIO and real sensors were not exercised.

`EmulateAcquisition.java` executes the original ADC and smoother instructions,
including nested application calls, with explicit Wire, clock, compiler
arithmetic and diagnostic boundaries. It rejects unknown calls, assertions
and unmodeled hardware loops. Those historical cases alone do not execute the
Core publication loop or any returning assertion.

The additional `adc-continuation-validation.json` records **255 complete
original comparisons**, including **206 executions of Core::tick** and
**597 executions of the real assertion helper**. It executes the ADC,
thermistor and smoother callees and checks the subsequent ordered Core
services through the caller's return. All **13,260 binary64 fields** match
exactly in the recorded environment. Other Core services, Wire, SDK clock,
delay, printf, compiler arithmetic and logarithm remain explicit boundaries;
this is not a complete hardware or RTOS emulation. The complete compressed
fixture is retained, with OEM function byte ranges and input/source hashes.

The old data-only API remains available without a diagnostic sink: it rejects
negative byte reads and zero publication time. These guarded calls are not
claimed as full OEM continuation. Nonfinite samples, malformed owned smoother
state and missing callbacks also remain explicit adapter rejections. Production
Core binds the sink and uses the continuation path.

```sh
python3 tools/validate_acquisition.py --prepare-input build/acquisition-input.json
# Use the preserved Ghidra project, -noanalysis -readOnly, and:
# -postScript EmulateAcquisition.java <absolute-input-json> <absolute-output-jsonl>
python3 tools/validate_acquisition.py --original-jsonl build/acquisition-original.jsonl \
  --export <preserved-export> --write-evidence
python3 -m unittest discover -s tests -p test_sensor_acquisition.py
```
