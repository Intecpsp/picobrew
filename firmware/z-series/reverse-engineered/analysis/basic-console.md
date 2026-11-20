# Z119 Basic diagnostic callbacks

`basic_console_tests.cpp` implements the 37 recovered Basic diagnostic leaves and
the complete `Core::basicTests` sequence at `0x4011ed44`, including its console
wrapper `0x4011fbac`. The aggregate invokes the separately recovered backend,
EEPROM, MAX11613, Runtime and Still self-tests directly, preserves the original
test order, and finishes with the Head test callback and original suite marker.
Unknown callbacks and missing or failed services return failure.

The source executes the diagnostic operations. It does not replay captured
messages as a substitute for those operations. Original statistical samples,
paragraph strings and the ProgramWriter binary example are immutable test data;
`tools/extract_basic_console_data.py --check` verifies their bytes against the
preserved OEM image. The original binary is not a runtime dependency.

## Recovered leaf bodies

| Diagnostic | Original address |
| --- | --- |
| ArrayIterT<int> | `0x4010e2b4` |
| BackoffTimer | `0x400e18a4` |
| Buffer<16> | `0x4010e580` |
| CString | `0x400e1e90` |
| CommandData | `0x401015c0` |
| CoreHelpers::SerialLogger | `0x400fa974` |
| EnumHelpers | `0x400e2c20` |
| EventQ | `0x4010f1c4` |
| HttpParser | `0x40109dbc` |
| HttpRequest | `0x4010cb60` |
| Instruction | `0x400e2f4c` |
| LinearInput | `0x400f195c` |
| LocalizationHelpers | `0x400e3004` |
| MacAddress | `0x400e32c4` |
| NetworkSessionState | `0x40117578` |
| NoiseSmoother | `0x4011272c` |
| Out | `0x400d79ac` |
| ParagraphText | `0x400e38dc` |
| Program | `0x400e3a54` |
| ProgramBuffer | `0x400e3f10` |
| ProgramResolver | `0x400ed224` |
| ProgramWriter | `0x400ecad0` |
| Quantity | `0x400e4544` |
| RecipeRef | `0x40101844` |
| Rect | `0x400eb870` |
| Safeties | `0x400e5158` |
| SessionTypeHelper | `0x4010e0e4` |
| StaticPrograms validation | `0x400ed2ec` |
| StaticPrograms CLEAN dump | `0x400e1048` |
| Stats::Computer | `0x400e55a0` |
| Stats::Rolling | `0x400f560c` |
| Temperature | `0x400db514` |
| TemperatureHelpers | `0x400db56c` |
| TextJustifier<21> | `0x40111df8` |
| TextLines | `0x4010f418` |
| UTF8Decode | `0x400e56d4` |
| WiFiCreds | `0x401011a4` |

The aggregate's 45 labelled groups additionally include the original log-only
HttpUpdate, ProgramParams and RuntimeState entries. StaticPrograms checks all
21 original built-ins in their recovered order and dumps CLEAN. Stats includes
the inline `sizeof(Stats::Rolling)` check. ProgramParser includes the two original
temperature-cap scopes; the compiler folded their constant expression checks,
as it did the Temperature/TemperatureHelpers checks. Their remaining scope
bodies still execute.

The NetworkSessionState data-object parser intentionally does not apply the
outer Azure transport's FirmwareVersion gate. Its original self-test document
uses `0.0.dev`; the outer transport separately validates firmware compatibility.
The diagnostic uses the selected and pinned ArduinoJson 6.21.6 dependency. The
exact minor revision in the OEM remains unknown.

## Full original execution comparison

The frozen fixture contains **152 cases**: four clock, stack-water and assertion
counter states for every leaf, plus four aggregate/wrapper cases. Those cases
execute **424 original functions**, including **651 calls to the actual returning
assertion helper**, and compare **27,337 ordered events**. The normal aggregate
alone produces 4,002 events. All 148 individual leaf cases match exactly.

The four aggregate cases have only the two already documented ZBackend logging
differences. The OEM passes an integer ValvePosition through a floating-point
vararg formatter, reading unrelated ABI words. The reconstruction prints the
checked integer. Comparison requires exactly one occurrence of each original
line per aggregate and projects only these two messages:

| Original message | Reconstructed message |
| --- | --- |
| `ZBackend.h:449 valvePosition1 = 100.000000` | `valvePosition1 = 1` |
| `ZBackend.h:494 valvePosition2 = 50.099976` | `valvePosition2 = 1` |

The fixture retains the raw original messages. Assertions, ordering, remaining
logs, state and time remain compared. This is a documented source correction,
not an exact-output claim for the two malformed original logs.

The stack-water cases straddle the original `> 700` threshold. The low-water
aggregate starts at clock `0xffffff80` and counter `0xffffffff`, executes 197
assertions, reaches the Head callback, emits `Basic Tests ERR`, and finishes with
counter 196 and clock 39,473. Every assertion updates the counter and adds 200 ms
before returning. The ordinary NoiseSmoother self-test delay adds another 201 ms.

Clock zero has a separate original behavior: the NoiseSmoother date uses zero
as an unset sentinel, so its delayed reset check fails. The reconstructed test
preserves that behavior. The aggregate continues and reports its resulting
failure; it does not replace that path with a successful shortcut.

## Services and boundaries

`ApplicationConsoleHooks::test_scope` is required for the original TestScope
protocol. It is distinct from the ordinary trace scope used by MacAddress's
assignment test. The SDK owner binds it to the original assertion counter,
stack-water checks, timestamp reporting and watchdog reset. The original SDK
address `0x40136e8c` is `esp_task_wdt_reset`.

`ConsoleTestMarker` implements `0x400ee670` and `0x4010a26c`; the aggregate uses
that shared implementation. Its actual Head display-message renderer and the
final `0x400fe350` Head test dispatch remain explicit boundaries in this Basic
fixture, with separate display/Head evidence. The hook must dispatch the real
Head body in the application.

The oracle validates every mapped original image segment before execution.
It runs actual application bodies, including nested assertion, program, parser,
thermistor, statistics and red-black-tree functions. Explicit environment
boundaries cover ROM strings/memory, finite scalar math, bounded formatting,
successful heap allocation/free, clock/stack/watchdog, output, C++ static guards
and reviewed Xtensa instruction adaptations. The transient Quantity disassembly
correction changes analysis metadata only. No firmware bytes are changed.

The evidence does not establish peripheral timing, RTOS behavior, actual memory
pressure, live network exchanges or hardware parity. Existing guarded policies
for malformed production recipe/program inputs retain their own evidence and
limits; successful embedded self-tests do not broaden those claims.

## Reproduction

Run the native comparison and data check from this directory:

```sh
python3 tools/extract_basic_console_data.py --check
python3 tools/validate_basic_console.py --write-report
python3 -m unittest discover -s tests -p test_basic_console.py -v
```

`tests/run_basic_console_emulation.py` regenerates original traces using a
private Ghidra project, the pinned Ghidra/JDK directories and a new output
directory. The checked fixture is compressed and includes the original
function/boundary execution counts.

`tools/build_basic_console_objects.py` verifies the dependency identities and
compiles both owned translation units with pinned ESP32 GCC 5.2. The target
report records their object/source hashes and included data/header identities.
The full application link is tracked separately.

The six native regression tests pass. A separate `-O1` ASan/UBSan executable
with `BASIC_CONSOLE_SANITIZER_MAIN` executes all 37 leaves, both aggregate paths
and the missing/failed-service checks. It exits successfully with no sanitizer
finding. Normal native and target compilation retain `-Werror`; the sanitizer
build alone uses `-Wno-error=uninitialized` for the pinned ArduinoJson
`StringMover::startPtr_` warning. Its warning log is retained with the build.
LeakSanitizer is disabled because this environment's tracing prevents it from
running, so this run does not claim leak detection.
