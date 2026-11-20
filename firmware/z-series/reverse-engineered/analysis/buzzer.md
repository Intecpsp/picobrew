# Z119 buzzer, software timer and System alarm

The owned `buzzer.hpp/.cpp` implements the original buzzer enable control,
LEDC setup sequence, Core's periodic buzzer callback, five console commands,
and the System alarm flag/date operation. `platform/esp32/buzzer_platform.*`
calls the real Arduino ESP32 1.0.1 `ledcAttachPin`, `ledcSetup`, `ledcWrite`,
`millis` and C-library decimal conversion. Console registration, returning
assertion handling and diagnostic output are explicit caller-owned services.

## Original evidence

The input is preserved `zseries_0_0_119.bin`, SHA-256
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.
The independent instruction harness verifies the complete mapped original
segments before executing, records each executed function's byte ranges and
hashes, and fails on any unreviewed SDK/library call.

| Original entry | Reconstructed operation |
| --- | --- |
| `0x400d141c` | Set enabled flag, configuring PWM only on a disabled-to-enabled transition |
| `0x400f6e78` | Attach pin, configure channel, write zero duty, then register commands |
| `0x40080570` | Core's buzzer software-timer callback |
| `0x400dbc40` | System alarm flag consistency check, diagnostic, timestamp and buzzer update |
| `0x400db014` | Returning assertion helper: diagnostic, global counter increment, 200 ms delay |
| `0x400f6d58` | Register `Buzzer` namespace and its five commands |
| `0x400d5d40` | `i`: information diagnostic |
| `0x400d144c` / `0x400d1460` | `on` / `off`: enable/disable wrappers |
| `0x400d08e0` / `0x400d08f4` | `setf ` / `setd `: decimal frequency/duty updates |
| `0x40120ca5..0x40120ce2` | Contiguous Buzzer member-initializer slice in the Core constructor |

The member initializer establishes channel **1**, resolution **8 bits**, pin
**13**, frequency **4800 Hz**, duty **125**, and three false flags: enabled,
previous enabled, and phase. This is evidence of original software
assignments; it is not an electrical pinout qualification on a physical Z.
The initializer slice starts with `a2=0`, `a4=Core`, and `a5=Core+0x4600`.
The surrounding original instructions establish those registers (`a2` is
zeroed at `0x40120beb`, `a4` receives Core at `0x40120a07`, and `a5` receives
`a4+0x4600` at `0x40120c74`). The harness executes only the 62-byte member
initializer slice, not the full Core constructor.

## Preserved behavior

Setup performs `attach -> configure -> write(0) -> console registration`.
`ledcSetup` returns an actual configured frequency; the original code ignores
that result, including zero. The owned service distinguishes an invoked SDK
operation from a missing or failed binding without changing that behavior.

The timer queries `millis()` on every invocation. If either enabled or
previous-enabled is true, it copies the current enable flag, toggles phase
while enabled (or clears phase while disabled), and writes either the stored
duty or zero. If both enable flags are false, it leaves phase and PWM untouched.
Core's separately verified startup creates the repeating 1,000-tick software
timer; this module does not independently create a second timer.

Disabling the buzzer changes the flag immediately but does **not** immediately
write zero duty. The following timer invocation does that when the buzzer was
active at its previous invocation. An enable followed by disable without an
intervening timer therefore need not write anything. Those distinctions are
preserved in the original comparisons.

System alarm changes first compare the stored alarm flag with the buzzer
enabled flag. A mismatch reaches the original assertion at `System.h:280`
(`0x118`). The assertion helper at `0x400db014` **returns**: it logs, increments
the global diagnostic counter at `0x3ffc2e84`, delays 200 ms, then executes
`RETW`. It does not terminate this operation. The owned source invokes an
explicit required returning assertion service and continues with the request.
Its `assertion_line` retains the last reported line for diagnostics.

A changed alarm reads the clock for diagnostic output, updates the flag,
reads the clock again for the saved timestamp, and calls the buzzer setter.
An unchanged alarm performs no functional clock or PWM operation, even if
the mismatch assertion occurred first. Consequently, asking for an unchanged
alarm can leave the mismatched buzzer flag unchanged, exactly as the OEM.
Assertion logging, global-counter ownership and its 200 ms delay belong to
the application assertion service. The fixture records that service boundary
without adding its diagnostic wall time to the independent functional clock.

The console commands retain exact names, descriptions, matching modes and
callback targets. `i`, `on`, and `off` match exactly; `setf ` and `setd ` match
prefixes. Frequency and duty commands only store the parsed value. They do not
immediately write PWM. Negative representable `int32` inputs preserve their
unsigned 32-bit pattern. Original ROM `atoi`, SDK operations, C++ console
storage and diagnostic formatting are explicit execution boundaries; decimal
overflow and byte-for-byte diagnostic formatting are outside the equivalence
claim.

## Verification

`analysis/buzzer-validation.json` records **561 independent original/native
scenarios** and source hashes. The scenarios execute eleven complete original
bodies, including the returning assertion helper, plus the initializer slice.
The helper replay verifies its diagnostic counter increment and 200 ms delay
argument before returning to the caller. The scenarios
cover all enabled/previous-enabled/phase combinations, repeated ticks,
start/stop sequences, zero and maximum unsigned frequency/duty values, clock
wrap, matching and inconsistent alarm flags, repeated continuation after returning
assertions, both functional clock samples, all console
commands, registration metadata, and deterministic varied pin/channel inputs.
The SDK calls are captured at their boundary; out-of-range synthetic settings
do not assert that the hardware accepts those settings.

The compressed portable fixture replays every scenario without Ghidra.
The composed test routes `HeadFlow::alarm` through the real recovered alarm
operation and timer, then compiles the ESP32 adapter with recorded LEDC calls.
It also checks failed and missing console/assertion/log services. AddressSanitizer and
UndefinedBehaviorSanitizer are enabled for that composition test.
`analysis/buzzer-target-build.json` records compilation of the actual module
and adapter against the pinned GCC 5.2 / Arduino ESP32 1.0.1 SDK in the
53-module archive at this checkpoint.

Reproduce original execution using the repository's verified original-image
Ghidra project (the harness transiently defines omitted address-taken entries):

```sh
python3 tests/run_buzzer_emulation.py \
  --project /path/to/z119-project \
  --ghidra-home /path/to/ghidra_12.2_PUBLIC \
  --java-home /path/to/jdk-25.0.5+8 \
  --output /new/output/buzzer-original
python3 tools/validate_buzzer.py \
  --original-directory /new/output/buzzer-original --write-evidence
python3 -m unittest discover -s tests -p test_buzzer.py
```

## Remaining integration boundary

The application must install the real console registry and diagnostic sink,
keep the bindings and Buzzer object alive, install the returning assertion
service, connect System's shared alarm state,
and invoke `Buzzer::tick()` from its existing Core software-timer callback.
Access from the Core task, console and FreeRTOS timer must be serialized by
the application. The portable object deliberately provides no default
successful no-op or invented global owner. Instruction replay uses one thread
and does not validate concurrent access or scheduling jitter.

This completes these owned buzzer/alarm operations. It does not establish a
complete linked application, bootable image, physical audio, flash support,
or Z1/Z2 hardware mapping.
