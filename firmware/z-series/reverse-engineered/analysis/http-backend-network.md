# Complete HttpBackend network diagnostic

`http_backend_network_console_test` reconstructs the original `0x4011b6cc`
body. It temporarily installs the original test identifier, performs all nine
requests, runs the actual parser and program builders, evaluates every assertion,
restores the identifier and transitions the backend to Idle.

The new comparison records **12 exact response cohorts**, **22,810 ordered
events**, **179 executed original function bodies** and **231 executions of the
real returning assertion helper**. Its diagnostic output, counter increment,
200 ms delay and continuation execute in the original image. All 24 final state
words and the restored identifier digest match the native reconstruction.

| Request state | Original parser used by the diagnostic |
| --- | --- |
| Backend state (`2`) | Registration/backend state |
| My recipes (`4`) | Recipe reference list |
| Shipped PicoPaks (`12`) | Recipe reference list |
| Current Z kits (`18`) | Recipe reference list |
| PicoPak reference (`16`) | Single recipe reference |
| PicoPak recipe (`14`) | PicoPak recipe and program builder |
| Zymatic reference (`8`) | Single recipe reference |
| Zymatic recipe (`6`) | **PicoPak parser**, preserving the OEM diagnostic quirk |
| Resumable session (`22`) | Session type and contained recipe/program |

The request loop preserves yield, clock read and HTTP tick order. TestScope
objects remain distinct from ordinary debug scopes. Missing services and failed
callbacks return failure; the temporary shared identifier is restored on every
such exit. Fourteen explicit native contracts check these exits, including failed
production transition and recipe-reference logging. ASan and UBSan pass all 12
response cohorts and all 14 failure paths. Three affected objects compile with
the pinned ESP32 compiler.

## Production corrections exposed by the complete path

The full comparison found state-transition information and single-reference
information missing from the earlier production reconstruction. The backend now
emits the exact original state names and transition message. Recipe-reference
logging emits the complete source name before its bounded copy and any
truncation warnings, in the original order. Existing standalone value-only
harnesses can omit the optional diagnostic sink; the concrete application binds
the real sink.

## Evidence and boundaries

The response cohorts cover clock/counter wrapping, low stack, invalid JSON,
wrong values, absent clients, connection failure, wrong content type, HTTP
failure status, short content, empty original identifier and an original
identifier equal to the test identifier. Real original HTTP configuration,
request transmission, response state machine, receiver callbacks, JSON parsers,
program builders and reports execute through return.

Socket and SDK results are prescribed. Reviewed ROM operations, scalar math,
fixed formatting, C++ guards, console rendering and the queue handoff are
explicit environment boundaries. The comparison does not contact a live vendor
service and does not establish RTOS, peripheral or hardware execution. Existing
guarded malformed-recipe rejection remains distinct from this exact matrix.

The original TestScope constructor, destructor and stack-watermark assertions
execute. Their console rendering is represented by begin/end notifications in
this comparison; the separate System console evidence checks that renderer.
The scope boundary therefore does not substitute a successful return for the
actual TestScope body or its returning assertions.

Private original protocol strings are recovered only into ignored build output.
Request writes, bulk response-body reads, host strings, formatted message
payloads and the restored identifier are hashed before evidence leaves the comparison processes. The
committed inputs contain prescribed responses and public fixture identifiers;
they do not contain generated credentials. Deterministic gzip preserves the
input and original-output fixtures.

`run_http_backend_network_emulation.py` regenerates the original fixtures from a
read-only, byte-verified Ghidra project. `validate_http_backend_network.py`
compares native behavior and records source identities. The target, sanitizer
and original toolchain reports are separate files. The sanitizer configuration
disables LeakSanitizer because the execution container uses ptrace; ASan/O1
allows only the pinned ArduinoJson StringMover uninitialized compiler warning.
Normal and target compilation retain warnings as errors.
