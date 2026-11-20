# Complete MSP diagnostic continuation

The application binds `z_msp_client::diagnostics` to its real shared assertion,
log, debug-scope, counter and watchdog-reset services. Bind this borrowed pointer after
`z_msp_init`, which clears client bookkeeping. The production path now executes
the original diagnostic-and-continue behavior. A missing required diagnostic
service returns an explicit integration failure; no transport or diagnostic
operation is represented by a successful empty application callback.

## Command and recovery behavior

Wrong-state and moving-valve assertions return before the command transmits.
Both assertions execute when both conditions fail. An out-of-range valve byte
likewise asserts, then sends its wrapped command and marks the valve moving
after processing the response. The response handler preserves BOOT/APP logs,
version reports, changed error/flag diagnostics, the bare-PCB diagnostic, and
unknown-response byte dumps in their original order.

Erasing stops on the first nonzero controller result, asserts, closes its debug
scope, and returns that raw controller byte. Update handles unexpected boot
versions, erase errors and programming errors by emitting the original message
and returning normally. Setup therefore still attempts the later application
start and version checks after a handled update failure. Its final major and
minor assertions are independent and both may execute. A bad image CRC also
asserts and continues; the preserved production image has the correct CRC.

The lower-level pure byte transformation functions remain independently usable.
A null diagnostic pointer explicitly selects the previous guarded interface for
legacy callers. Its thirteen regression tests and old assertion-sentinel fixture
still describe that bounded interface, and are not evidence for full execution.

## Concrete diagnostic commands

| Console callback | Original diagnostic body |
| --- | --- |
| `0x400dc1a4` | 200 application-version reads |
| `0x400dc970` | 50 boot/application switches with version reads |
| `0x400dd268` | Up to 10 unit-test-image updates followed by production recovery |
| `0x400dd29c` | Bootstrap; 10 switches; 10 reads; up to two update tests; error register tests |

Bootstrap actually switches, erases and performs production setup. The error
test writes `0xdeadbeef`, checks the returned register, clears it and checks zero.
The update test verifies 255.255 for the embedded test image, restores the
production image and verifies 1.0. It exits early only when the shared assertion
counter, interpreted as **signed** 32 bits by the original `BLTI`, is positive.
A wrapped negative count continues and resets the task watchdog. The tests invoke the
real transport supplied to the firmware; native verification supplies explicit
device responses and does not contact a controller.

The bootloader-version console callback now prints valid nonzero version bytes
instead of treating them as an unsuccessful operation. The standalone bootstrap
console command executes its complete scoped diagnostic body.

## Evidence and limits

All 123 original cases match across 40 functions and 30,975 event records,
including 320 real returning assertions and 227,191 actual CRC-byte executions.
Seven focused native groups and the thirteen legacy regression groups pass.
The pinned ESP32 build compiles all 108 local modules/adapters plus nine
Ethernet2 dependency objects; the three changed product objects are recorded
in `msp-continuation-esp32-build.json`.

The new original-execution fixture compares complete bus frames, response bytes,
state bytes, diagnostic order, controller return values, counter overflow and
clock wrap. Original assertion and both ScopeLogger implementations execute
through return. Only SDK/environment services, reviewed soft-float primitives,
formatted-output capture and platform scope text rendering are boundaries.

Every mapped byte in the analyzed program is checked against the preserved OEM
image before execution. The original full-image CRC function executes on both
unchanged and deliberately corrupted **fixture data**, then its verified result
is cached for repeated setup calls. The firmware image on disk is unchanged.
Every packet CRC executes its actual original eight-iteration loop.

The deterministic gzip records, source identities and exact execution totals are
in `msp-continuation-validation.json`. No hardware, physical installation or
brewing verification is claimed by these native and instruction comparisons.

```sh
python tools/validate_msp_continuation.py --write-report
python -m unittest discover -s tests -p 'test_msp*.py' -v
```

The SDK call at `0x40136e8c` is `esp_task_wdt_reset`. Its earlier
`SDK_stdout_flush` label was incorrect. The SDK owner now binds the actual task
watchdog operation, and all 123 original continuations have been re-executed
with the corrected boundary identity. Test scopes retain their separate begin
watchdog reset and end-of-scope stack/assertion checks.
