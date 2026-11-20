# Z119 SerialLogger and Core CSV

`serial_logger.hpp`, `serial_logger.cpp` and `core_csv.cpp` reconstruct the
SerialLogger at `0x400efcfc` and the concrete Core CSV provider, with owned
384-byte text storage. The source consumes the existing heater, temperature,
auxiliary sensor, MSP, runtime and PicoStill state types. `CoreCsvSources` borrows
those long-lived application states; `core_csv_source` supplies the concrete
header/data callbacks. Required diagnostic/source callbacks return an explicit
missing-service or service-failure result.

| Original body | Recovered behavior |
| --- | --- |
| `0x400efa4c` | Reset buffer, call header provider, latch truncation, returning assertion at CoreHelpers.h `0x3a`, then emit CSVHDR. |
| `0x400efb04` | Reset buffer, call data provider and latch truncation without emitting a row. |
| `0x400efb34` | Regenerate; returning truncation assertion at `0x41`; emit CSTA row at `0x42`. |
| `0x400efaac` | Any change in enable state emits a fresh header, including disabling. |
| `0x400efcfc` | Emit only when enabled and `deadline < now`; assign `deadline = now + interval` before output. Unsigned wrap behavior is retained. |
| `0x400efb94` | Emit True/False enable information and interval, then header and data. |
| `0x400d76ec` | Parse signed decimal interval into the uint32 field; clamp unsigned values below the minimum. Negative signed values become large unsigned values. |
| `0x400fae40` | Register `setinterval `, `i`, `start`, `stop`, `on`, `off` in `Serial Logger`. Only `setinterval ` is a prefix match. |

Constructor defaults recovered from Core are disabled, deadline zero, interval
1000 ms and minimum interval 200 ms. Application composition registers the
console explicitly; it does not run test or information commands during setup.

## Concrete CSV provider

The header at `0x400f5d08` contains the exact five groups and comma separators.
`0x400f6064` calls all five concrete data formatters in the same order:

1. Heater target, output duty, lowercase PID boolean and displayed integral
   (`0x400f5e40`).
2. Filtered/instantaneous wort and HEX temperatures, their rates, drain/ambient,
   pressure, altitude, and boiling temperature (`0x400f5e74`). Altitude uses
   `(1 - pow(pressure / double(reference_float), 0.1903)) * 44330` with the
   original float-width reference pressure.
3. MSP error/fan/pump bytes, pump text, valve byte/text, and the two status bytes
   labelled Stepper/StateFlags by the original CSV (`0x400f5f40`). A moving valve
   prints `****`. Pump upper nibbles 8–15 read the adjacent original valve table;
   the explicit 16-entry source table retains those observable strings without
   an out-of-bounds C++ access.
4. Runtime total/instruction seconds remaining, opcode name, signed argument,
   signed step index and step name (`0x400f5f94`).
5. PicoStill temperatures, pressure and sample standard deviations, using zero
   for sample counts below two (`0x400f5fcc`, `0x400ef7e4`).

The 384-byte buffer preserves the original distinction between append and printf:
raw append truncates on `count > remaining`, whereas printf marks truncation on
`formatted_count >= remaining`, including an exactly full result. Both preserve
a terminating NUL. Returning frozen/null/truncation assertions continue through
the reviewed original branches. `ever_truncated` is a latch independent of the
current row. The logger does not silently omit a truncated row after an assertion.

`SerialLogServices::emit` carries Header, Status, Enabled, Interval,
BufferTruncated, DroppedBytes and IntervalBelowMinimum records. A platform renderer
can reproduce the original CSTA/LOG/BUF prefixes and severity, while a shared
assertion service performs logging, counter update and 200-ms delay. Raw strings
passed to emit are borrowed only for that call.

## Evidence

`serial-logger-validation.json` and deterministic
`tests/fixtures/serial-logger-original.json.gz` record **334 exact original/native
comparisons**. The original executor runs the complete logger bodies, complete
Core header/data formatters, bounded string operations and actual returning
assertion helper. Cases cover deadline equality/wrap, changed and unchanged enable
state, interval parsing, exact-fit/oversize text, frozen buffers, sticky truncation,
all pump upper nibbles, valve limits and moving state, invalid opcode names, long
step text, random finite states, standard deviations, and overflow across multiple
formatter groups. No assertion is treated as terminal.

SDK `vsnprintf`, ROM `atoi`, clock/diagnostic output and libm are explicit
boundaries. The formatter model decodes the real Xtensa va_list register/stack
layout and rounds binary double values to even; original argument selection,
ordering, buffer accounting and state mutation still execute from firmware
instructions. ROM decimal parsing is modeled in its 32-bit signed domain. Live
SDK libc, UART traffic, hardware timing and physical samples are not claimed by
this comparison. Generic Shell registration is separately tested by the shared
console evidence.

The target build report covers `serial_logger.cpp`, `core_csv.cpp` and their
header with the pinned ESP32 compiler. It is an object build, not a complete
application link or a hardware validation.
