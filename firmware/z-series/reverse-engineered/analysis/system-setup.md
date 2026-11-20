# System setup and returning diagnostics

`system_setup.hpp/cpp` reconstructs Z119 `System::setup` at `0x40112f88`,
including its console registration at `0x400f7f48`. The ESP32 adapter performs
the partition queries, SPI and I²C initialization, two bus scans and watchdog
calls against the pinned historical SDK. Application bindings supply the
console registry and must remain alive for the System lifetime.

The original assertion helper at `0x400db014` returns. `diagnostics.hpp/cpp`
preserves its order: sample the unsigned millisecond clock, convert to float
seconds, log, increment the shared unsigned counter, and delay 200 ms. Both
watchdog failures are diagnosed independently; even two failures are followed
by console registration. Adapter failure is a separate result from an OEM SDK
error and never silently reports successful initialization.

The seven System commands and Buzzer namespace preserve the original tokens,
descriptions and exact/prefix distinction. This module registers descriptors;
the command implementations and generic shell dispatch are separate modules.

## Evidence

`system-setup-validation.json` records **200 matching cases** from execution of
the original instructions through return, including the assertion helper.
Cases cover both watchdog return values, signed error encodings, null/non-null
partition handles, float timestamp rounding, millisecond wraparound and
assertion counter wraparound. The comparison includes the ordered SDK effects,
diagnostic timestamp bits, counter values at log and delay, final partition
state and all command descriptors. `tests/test_system_setup.py` also checks
missing services and failure at each adapter boundary.

The oracle models SDK operations, bus scan internals, shell allocation and
registration, closure lifetime, arithmetic helpers, formatted serial output
and delay. It does not establish console callback execution, watchdog/ISR
behavior or physical initialization on a Z machine. The target object report
records compilation of the concrete adapter and diagnostics; no complete
application link or installation is claimed by this evidence.

To regenerate the comparison with the preserved Z119 Ghidra project:

```sh
python3 tools/validate_system_setup.py --prepare-input /tmp/system-cases.json
# Run tools/ghidra/EmulateSystemSetup.java with that JSON and an output JSONL.
python3 tools/validate_system_setup.py \
  --original-jsonl /tmp/system-original.jsonl \
  --export /path/to/z119-callback-export --write-evidence
python3 -m unittest discover -s tests -p test_system_setup.py -v
```
