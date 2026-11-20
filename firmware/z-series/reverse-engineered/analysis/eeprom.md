# Original Z119 EEPROM storage

`include/zseries/eeprom.hpp` and `src/eeprom.cpp` reconstruct the original data
format, validation, setup/write/reset lifecycle, and console setters from the
preserved `zseries_0_0_119.bin`. This closes the storage service used during Core
startup. It is not a replacement flash driver or a claim of a complete bootable
application.

## Application diagnostic continuation

The application now obtains diagnostic services and the full EEPROM self-test
from `ApplicationConsole::eeprom_services()`. With that binding, initialization
and size assertions execute the real returning helper and subsequent storage
work continues. [eeprom-continuation.md](eeprom-continuation.md) records the full
2,363-case replay and exact logging. The old typed assertion guards and original
sentinel fixture below remain explicitly limited to a null diagnostic pointer.

## Data layout

The original record is exactly 120 bytes. Encoding and decoding use explicit
little-endian fields, so host compiler padding does not change the flash format.

| Offset | Bytes | Recovered field |
|---|---:|---|
| `0x00` | 4 | Schema version, valid value `0` |
| `0x04` | 1 | `useEthernet` |
| `0x05` | 1 | `useImperial` |
| `0x06` | 1 | `showDetail` |
| `0x07` | 1 | `slideshowCycleStatic` |
| `0x08` | 4 | `LanguageType` |
| `0x0c` | 32 | Wi-Fi SSID, including terminator |
| `0x2c` | 64 | Wi-Fi password, including terminator |
| `0x6c` | 4 | `SessionMode` |
| `0x70` | 4 | `BoolOption` for Azure |
| `0x74` | 4 | `BoilerType` |

All four flag bytes retain their raw values during round trips. The original
enum repair function normalizes language, session mode and Azure values greater
than 2 to zero, and boiler values greater than 1 to Big (zero). It does not
normalize the flag bytes or touch strings/version.

The separate validity predicate requires schema 0, a NUL within each string
array, and boiler Big or Small. It deliberately ignores the other three enum
values because setup repairs them separately.

## Original lifecycle

The Core constructor owns an Eeprom object at Core + `0x4788`. Its record starts
at object + 8, initialized byte at `0x80`, and requested SDK buffer size at
`0x84`. The constructor requests 512 bytes.

Setup calls the SDK's `EEPROM.begin`, asserts a requested size of at least 120,
and copies the record if the SDK's actual cache contains at least 120 bytes.
It marks the object initialized, repairs enums and writes if necessary, then
resets invalid data and writes again if necessary. Console registration follows
all storage work. Invalid enums together with an invalid version or string can
therefore produce two commits, in that order.

Write copies the 120-byte record into the SDK cache and marks it dirty when the
actual cache is large enough, then calls commit regardless of cache size. Reset
preserves a valid Big/Small boiler selection and clears every other record byte;
an invalid boiler becomes Big. Both write and reset assert initialization.

The original code ignores the boolean return values of begin and commit. The
reconstruction preserves that control flow and exposes the statuses through
`last_begin_succeeded` and `last_commit_succeeded`. It does not invent recovery
or persistence guarantees absent from the original instructions.

## SDK and application integration

`arduino_eeprom_backend(EEPROM)` instantiates four callbacks against the existing
Arduino ESP32 1.0.1 `EEPROMClass`: `begin`, `get`, `put`, and `commit`. Partition
lookup, allocation, flash erase/write, cache size and dirty state remain owned
by that library. Its header is `libraries/EEPROM/EEPROM.h`, without a `src`
directory. No third-party flash code is copied here.

`EepromServices::register_commands` is an explicit application console dependency.
Setup returns `MissingRegistry` if it has not been bound; completed storage work
is retained. The application should register the nine descriptors returned by
`eeprom_commands` and route their callbacks to `EepromStore::command`. Info also
requires a `report_data` callback. SSID, password, Azure, Ethernet and boiler
commands change the in-memory data without committing it automatically, matching
the original callback bodies.

The original text boolean parser treats only null, `"0"`, `"off"`, and `"false"`
as false. Comparison is case sensitive, with no trimming; a non-null empty string
is true. String setters truncate to capacity minus one, write a terminator, and
leave bytes after that terminator untouched. Validation uses nonoverlapping
source strings, as required by the original `strncpy` boundary.

The public API reports original initialization/size assertions as typed results
with their original source line. Missing callbacks and invalid command IDs are
additional explicit integration errors. It does not run an assertion handler
against hardware in native tests.

## Original addresses

| Entry | Reconstructed operation |
|---|---|
| `0x400d1a2c` | Zero/default data constructor |
| `0x400d6410` | Enforce enum validity |
| `0x401bf7f0` | Validate record |
| `0x400fbc1c` | Setup |
| `0x400edb34` | Write |
| `0x400edc18` | Reset |
| `0x400de248` | Initialized data getter |
| `0x400d0ed4`, `0x400d0e60`, `0x401bf254` | Text boolean/equality |
| `0x400d598c`, `0x400d585c` | Bounded string setter |
| `0x400d59ac`, `0x400d59c0` | SSID/password callbacks |
| `0x400d6760`, `0x400d67a8` | Azure/Ethernet callbacks |
| `0x401bdebc`, `0x401bdee0`, `0x401bdf04` | Unknown/Big/Small boiler callbacks |
| `0x400fba48` | Console descriptor registration, explicit fixture boundary |
| `0x400dd2e0` | Embedded data test, basis for the portable self-test |

The getter was classified as data by the initial direct-call import. The test
script defines only its verified 30-byte `ENTRY` through `RETW` region in a
read-only analysis view, using the matching Xtensa objdump as a second decoder.
The original file and retained project are unchanged.

## Acceptance evidence

`tests/fixtures/eeprom-original.json` contains 2,363 bounded original-instruction
cases. `analysis/eeprom-validation.json` pins the OEM image, executed function
ranges, generator files, source files, Ghidra/JDK archives, fixture and execution
limits. Four native regression groups compare data bytes, cache bytes, initialized
and dirty state, assertion lines, return values, and ordered begin/commit/register
events. The same test instantiates the real adapter shape against a bounded cache
fixture, verifies all-byte round trips, and exercises missing dependencies.

The original instruction harness normalizes only reviewed Xtensa register-window
call/return boundaries. SDK begin/commit outcomes and cache size are explicit
inputs; flash hardware, allocation and console internals are not emulated.
ROM string/memory and diagnostic services are explicit boundaries. Any unreviewed
hardware-loop instruction fails the run. No physical I/O was performed.

Both the storage module and an adapter instantiated against the actual unmodified
Arduino ESP32 1.0.1 header compile with the pinned Xtensa GCC 5.2 toolchain. The
normal `make verify` discovers EEPROM tests alongside runtime, heater and MSP
integration tests.

To regenerate the original fixtures, use `tests/run_eeprom_emulation.py` with
`--project`, `--ghidra-home`, `--java-home`, and a new `--output` directory. Its
default full run is bounded to 600 seconds and 30,000 instructions per case.
