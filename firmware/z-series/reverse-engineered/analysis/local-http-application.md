# Incoming HTTP application and ESP32 sockets

`local_http_application.*` connects the recovered incoming server to the actual
recipe parser, owned `ProgramBuffer`, Core event mailbox and live control state.
`platform/esp32/local_http_platform.*` connects its socket operations to the
historical Arduino ESP32 `WiFiServer`/`WiFiClient`. The earlier unverified draft
is superseded by the source-bound evidence recorded here.

## Original behavior and source boundaries

The preserved Z119 application has SHA-256
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.

| Original entry | Reconstructed behavior |
| --- | --- |
| `0x401172b8` | POST dispatch: exact `/recipe` path, freeze mutable request data, parse recipe, send empty 200 or bounded text 400 |
| `0x40116968` | Parse original local-recipe JSON, build the owned program, derive parameters/statistics, reset the Core event and publish readiness |
| `0x401bf484` | Reset event metadata while retaining the original arguments and twenty recipe references |
| `0x400f02e4` | Copy the complete event into an empty one-slot mailbox; retain an already queued value |
| `0x400db014` | Log a diagnostic assertion, increment the counter at `0x3ffc2e84`, delay 200 ms, then return to the caller |
| `0x400d97c4` | Previously recovered HTTP/1.0 response writer, bound concretely by `LocalHttpApplication::bind()` |
| `0x4011ae84` | Previously recovered `/state` serializer, bound to current sensors, heater, MSP, runtime and RSSI |

The parser consumes mutable JSON, as the original does. It copies recipe and
step names into `ProgramBuffer`; later reuse of the socket buffer does not
invalidate the resulting program. A successful recipe queues `ProgramLoaded`.
An occupied mailbox drops the new event while the shared event object and
program still update, preserving the OEM ordering.

Unknown POST paths return `400 BAD REQUEST` with the eleven-byte body
`BAD REQUEST`. They do not parse or reset the current program. A JSON error on
`/recipe` resets the program, retains event/mailbox state and returns the
original `deserializeJson error <code> <name>` text. The original parser's
case-sensitive paths, framing and JSON compatibility limits remain those
documented in `local-http-server.md` and `program-recipes.md`.

The SDK adapter constructs port 80 with four pending clients. Accept is
nonblocking; connection presence is observed separately. Listener failure and
short writes propagate as service failures. The application retains the
original ordering in which program publication occurs before response writes;
a failed response does not undo an already published program.

## Validation

`local-http-application-validation.json` records **68 composed original
executions through 70 instruction bodies**. **62 cases match exactly**. Each
comparison checks response status, MIME, length and body; program instructions
and all copied strings; parameters and statistics, including double bits;
freeze state; and complete current/queued event payloads and pending state.
The interpreter executes the original JSON parser, program writer, event reset
and mailbox copy together. It does not replace those operations with mocked
application services. Preserved-image byte ranges are checked before execution
and again in the portable fixture test.

Six cases (`null`, `[]` and `{}`, each with empty/occupied mailbox) execute
**original assertions at lines 339 and 626 and continue normally**. Both calls
increment the diagnostic counter and request a 200 ms delay. The OEM creates a
LocalBrew program with an empty name, stores the empty string, emits Setup
`(57, 256)` and CompleteBrew `(40, 0)`, sets water to 11.36 L and duration to
60 seconds, and returns empty HTTP 200. It resets the shared Core event to
`ProgramLoaded`, retaining its arguments and recipe references. An empty mailbox
receives that event; an occupied mailbox retains its previous event.

The existing reconstruction deliberately preserves its stricter bounded
`ProgramBuffer` policy: the first empty name produces `EmptyString` and HTTP
400 with `program build error 3`. The buffer has LocalBrew type and ID zero but
retains the `<EMPTY>` sentinel, no instructions or strings, and default
parameters/statistics. The prior current event, mailbox and pending flag remain
unchanged; no readiness event is published. These are explicit input-validation
deviations from **defined OEM continuation**, including program and event state,
not full-state equivalence. The source retains the first diagnostic line as
error metadata and does not reproduce the two original counter increments or
delays. This checkpoint does not change that shared policy.

The earlier harness incorrectly modeled the assertion helper as an exception.
Its six partial observations are superseded by the fresh complete outputs in
the checked fixture. The updated executor runs the helper's actual counter
increment and return instructions, records both diagnostic calls and delays,
and executes the subsequent parser, program and publication instructions.

Original execution retains reviewed Xtensa calling-window handling, sequential
`MEMW`, scalar math and ROM memory/string boundaries. Error-message formatting,
diagnostic output/delay and the separately recovered HTTP response writer are
explicit services. This does not emulate the network stack, concurrent tasks
or hardware.

`tests/local_http_application_integration.cpp` sends actual HTTP requests through
`LocalHttpServer` and `LocalHttpApplication`, the selected hash-pinned JSON
adapter, recovered program builder and real Core publication. It covers:

- One-byte and uneven request fragments, repeated client acceptance and frozen
  buffer reset between requests.
- Recipe data ownership, loading the result through the real Runtime resolver,
  and retaining an already pending Core event.
- Unknown paths, malformed JSON, explicit bounded builder failure, and the
  original 2047-byte request-body limit.
- `/state` sampled from changed live control/runtime values after binding.
- Header/body short writes, socket close, publication-before-response ordering
  and missing-service failure.

`local-http-application-target-build.json` records compilation of both the
concrete application and actual WiFi socket adapter against the hash-pinned
GCC 5.2 / Arduino ESP32 1.0.1 toolchain. The full verification build contains
53 source/adapters. It produces an archive, not a linked or installable image.

## Reproduce

```sh
export ARDUINOJSON_DIR=/path/to/pinned/ArduinoJson-0b03321ed33fa6965114424fbb023260120c856b
python3 -m unittest discover -s tests -p 'test_local_http_application.py'

python3 tests/run_local_http_application_emulation.py \
  --project /path/to/verified/z119-project \
  --ghidra-home /path/to/pinned/ghidra_12.2_PUBLIC \
  --java-home /path/to/pinned/jdk-25.0.5+8 \
  --output /new/original-http-application-output

python3 tools/validate_local_http_application.py \
  --original-directory /new/original-http-application-output
```

Use `--write-evidence --target-report /path/to/build-report.json` only after a
fresh historical target compilation to replace the checked reports. Original
execution requires the already documented, verified read-only Ghidra project;
the portable tests require no Ghidra installation.

## Remaining integration

`LocalHttpApplication` and `LocalHttpNetwork` must outlive the server callbacks
they install. The complete Core/platform assembly must own those objects, call
`enable()` from the recovered Core Wi-Fi event path, tick the server, and bind
the original HTTP console registration (`0x400f7dc8`, commands `i`, `start`,
`stop`). This slice does not establish those application-lifetime bindings, a
complete firmware link, Z1/Z2 identity, flashing, OTA, boot or brewing behavior.
