# Complete EEPROM assertion continuation

`EepromServices::diagnostics` supplies the application's actual ConsoleServices.
`ApplicationConsole::eeprom_services()` binds it automatically, along with the
complete EEPROM diagnostic test body. Get, write, reset and setup now execute
the original returning assertions and continue after the 200 ms diagnostic
delay. Setup still ignores unsuccessful SDK begin/commit statuses as the OEM
does; the statuses remain inspectable.

An undersized requested buffer no longer prevents the subsequent read,
normalization and registration when diagnostics are bound. The SDK cache's
**actual** size independently controls whether its get/put copies 120 bytes.
Reset before initialization emits its own assertion, resets data, then invokes
write, whose initialization assertion also returns before commit. The getter
returns its data pointer after its assertion. No persistence guarantee is
inferred from an unsuccessful commit.

The source now preserves the enum information report, per-field error messages,
auto-fix warning, invalid-data error and save/reset messages. The EEPROM console
self-test uses these same concrete report/normalization functions. Required
diagnostics and self-test bindings cannot silently succeed when absent. A null
diagnostic pointer retains the older explicitly guarded interface for legacy
callers and the original historical fixture.

The full established 2,363-case data/lifecycle matrix has been rerun through
actual original instructions with the real assertion helper enabled: 21 original
functions, 12,469 recorded events and 391 real returning assertions all match. The new
evidence compares all record/cache bytes, initialized/dirty flags, diagnostic
order, storage calls, assertion counter overflow and clock wrap. Original
registration is a typed boundary covered by the independent console-registry
evidence. ROM memory/string operations and SDK cache/begin/commit behavior are
explicit environmental inputs. No flash hardware was accessed.

`eeprom-continuation-validation.json` records the exact execution totals and
source/fixture identities. The fixtures use deterministic gzip. The original
binary on disk remains unchanged; the missed getter is decoded only in the
read-only Ghidra analysis view.

```sh
python tools/validate_eeprom_continuation.py --write-report
python -m unittest discover -s tests -p 'test_eeprom*.py' -v
```

Validation passes five new continuation tests, four legacy EEPROM tests, seven
application-console tests and four hardware diagnostic tests. The pinned ESP32
toolchain compiles all 108 current recovered modules and nine Ethernet2 dependency
objects; `eeprom-continuation-esp32-build.json` records this build and the three
affected object identities.
