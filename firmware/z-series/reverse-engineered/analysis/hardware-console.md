# EEPROM and ADC diagnostic commands

`hardware_console_tests.cpp` implements the original EEPROM diagnostic command
`0x400dd678` and MAX11613 command `0x400de068`. ApplicationConsole calls these
bodies directly, using its real diagnostic and ScopeLogger services.

The EEPROM test executes all five original phases: enum normalization, data
validation, reset, the scoped enum-validation phase, and default Azure enablement.
Enum normalization preserves the ten-field information report before validation
and the four individual invalid-enum diagnostics. The scoped enum-validation
phase contains no predicate between its original constructor and destructor;
that observed empty body is explicit. All test data is temporary, as in the OEM.

The ADC command executes its scoped invariant phase and tests every raw value
from 15 through 4094 for all four thermistor types, then logs each type's two
endpoint temperatures. It does not communicate with the physical ADC.

## Original execution evidence

Four cases execute the original diagnostic bodies and their actual callees in
the preserved Z119 image. They cover normal and low stack high-water conditions
for each command. The low-stack cases execute the real returning assertion
helper, including counter overflow and 200 ms delays across clock wrap.
All event sequences, final counters and clocks match. The records include ten
original functions, 32,656 actual thermistor conversions and seven actual
assertion-helper executions. Each of the fourteen ScopeLogger constructor and
destructor calls executes its original instructions; platform rendering of the
scope text is a declared boundary, while begin/end order and low-stack assertions
are compared. No data-test or thermistor body is replaced by a successful stub.

The pinned ESP32 toolchain compiled all 107 local recovered modules/adapters and nine Ethernet2 dependency objects. `hardware-console-esp32-build.json` records the exact two owned objects and toolchain identity. The four focused native tests and the six existing ApplicationConsole tests pass.

Fixtures use deterministic gzip. `EmulateHardwareConsoleTests.java` records
executed functions and each environmental boundary, including the finite
positive logarithm, soft-float operations, ROM memory functions, SDK clock,
stack-water and task watchdog services. These comparisons do not establish physical
device behavior or brewing validation.

```sh
python tools/validate_hardware_console.py --write-report
python -m unittest discover -s tests -p test_hardware_console.py -v
```
