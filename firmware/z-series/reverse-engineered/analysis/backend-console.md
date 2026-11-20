# Backend diagnostics and Core test markers

The reconstruction now executes the complete HttpBackend data tests at
`0x401188f0`, ZBackend data tests at `0x4011dc20`, and their registered console
commands (`0x40119378` and `0x4011ec38`). These use the production JSON parsers,
program writer, full program reports, session serializer and log-event
encoders. The test documents are the public synthetic documents embedded in
the preserved Z119 image; no account credentials are included.

The shared `ConsoleTestMarker` implements `0x400ee670` and `0x4010a26c`, including
the assertion-count snapshot, elapsed time with unsigned wrapping, colored
output, Head message, and the returning failure assertion. `test_scope` is the
separate TestScope service; `scope` remains the ordinary debug trace service.

## Evidence

| Comparison | Cases | Result |
| --- | ---: | --- |
| Complete HttpBackend data suite | 4 | Exact ordered diagnostics, state, counter and clock |
| Complete ZBackend data suite | 4 | Exact except the two explicit malformed-vararg log lines below |
| Core test marker | 15 | Exact, including empty category, failures and counter/clock wrapping |
| MSP log-event encoder | 8 | Exact |
| Safety log-event encoder | 16 | Exact |

The original executor runs 176 application function bodies and 49 executions
of the real assertion helper `0x400db014`. That helper updates the counter,
delays 200 ms and returns; assertions do not stop the comparison. Every mapped
image segment is byte-verified before execution. Fixtures are deterministic
gzip files, and the raw original traces remain unchanged.

The original ZBackend test passes an integer ValvePosition to `%f` at lines
449 and 494. In the captured Xtensa ABI those calls print `100.000000` and
`50.099976`, respectively, by consuming an unrelated second argument word.
The reconstruction uses `%d` and prints the checked integer `1`. The validator
requires those exact original events and substitutes only those two messages;
all remaining events, checks, state and timing are compared directly. There
are 43 fully exact cases and four cases with this documented difference.

ROM memory/string helpers, scalar compiler math, reviewed clear/conversion
instructions, bounded formatting, SDK clock/stack/watchdog operations, C++
guards and diagnostic rendering are explicit execution boundaries. The Head
marker rendering is a typed boundary to the separately reconstructed display
owner. These cases perform no live cloud requests or physical peripheral
operations. They do not establish whole-device hardware parity.

## Production integration

The full application binds its `ConsoleServices` to `HttpBackend::diagnostics`.
The parser then emits the original list, registration, recipe, generated
instruction, parameter and statistics reports. Missing or failing diagnostic
services propagate a service failure. An entirely empty sink retains the
older value-only API for callers that deliberately omit diagnostics.

`finish_program` and `build_http_recipe` accept a final optional diagnostic
sink. Existing bounded rejection policies for malformed recipes and invalid
writer inputs remain unchanged and are documented by the separate recipe
continuation evidence. The successful original self-test documents exercise
the complete writer output; they do not erase those policy differences.

The pinned ESP32 GCC 5.2 compiler builds the nine changed or new production
units, including the console dispatcher. The normal native build uses strict
C++11 warnings. The complete preexisting recipe/repository comparisons and
the fresh 32-bit recipe replay also pass. The console replay checks 21 original
registries, 297 normal commands, three software-test dispatches, and the two
new backend-test dispatches.

A focused AddressSanitizer/UndefinedBehaviorSanitizer executable covers both
full suites, marker wrapping/failure, event encoders and failed service hooks.
It passes with leak detection disabled because this execution environment uses
ptrace. That instrumented build retains the selected ArduinoJson dependency's
`StringMover` initialization warning as a warning; normal native and target
builds continue to use warnings as errors.

## Reproduction

Set `ARDUINOJSON_DIR` to the checkout verified by
`dependencies/arduinojson.json`, then run:

```sh
python tools/validate_backend_console.py --write-report
python tools/validate_application_console.py --write-report
python -m unittest discover -s tests -p test_backend_console.py -v
```

`tests/run_backend_console_emulation.py` takes an existing private Ghidra
project directory, the pinned Ghidra and JDK locations, and a new output
directory. It verifies the tool identities and preserved image, opens the
project read-only without analysis, and executes the original functions.
`tools/build_backend_console_objects.py` takes the pinned toolchain, Arduino
SDK and ArduinoJson directories plus an output directory. Its report records
the exact source, dependency-header and object hashes. Neither tool flashes
hardware.
