# Z119 backend and Azure services

This layer supplies concrete HTTP request/result handling, session upload JSON,
Azure lifecycle and message parsing, credential construction, and console
registration. It targets the preserved `zseries_0_0_119.bin`, SHA-256
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.
The registration, program-download trigger and session logger that call these
services are documented separately in `core-helpers.md`.

## Recovered source

| Source | Original bodies and behavior |
| --- | --- |
| `src/http_backend.cpp` | `0x4011c30c` tick, `0x4011b250` request dispatch, `0x400fb1c0` Core commands, `0x400fafd4` state transitions, `0x401108e8` errors, registration trigger and receiver callbacks. |
| `adapters/arduinojson/http_backend_json.cpp` | Request JSON; `0x40117dac` registration; `0x40118060` reference; `0x40118218` lists; Zymatic, BrewPen, PicoPak and resumable-session wrappers using the concrete program builders. |
| `src/z_backend.cpp` | Core `+0xfff0` ZBackend; `0x40102120` tick, `0x40111984` endpoints, `0x40119c44` create, `0x40119ce4` end, `0x4011c4f4` logs, response-header/body/completion handling. |
| `adapters/arduinojson/z_backend_json.cpp` | `0x401199e4` session JSON, session-log JSON, `0x40117364` created-session IDs, including parsed IDs retained after a JSON error. |
| `src/azure.cpp` | `0x400f400c` lifecycle, `0x400ed990` transitions, nested connection attempts, retry state, session windows, `0x400fa400`/`0x4010dcbc` SAS construction and setup strings. |
| `adapters/arduinojson/azure_json.cpp` | `0x401179a8`, `0x40114b38`, `0x40117490` and parameter readers: command routing, group sessions, firmware gating, owned command envelopes. |
| `src/backend_console.cpp` | `0x400f7dc8` HttpServer, `0x401122a0` HttpBackend, `0x40112378` ZBackendT and `0x400ff6e8` Azure registration; concrete start/stop/connect/disconnect/session/publish callbacks. |
| `platform/esp32/azure_crypto.cpp` | Required SHA-256 HMAC primitive implemented using pinned ESP32 mbedTLS. Key bytes are supplied by application configuration. |

The HTTP services use the recovered `HttpClient` directly in production. They
require network-availability, diagnostic and returning-assertion services, and
explicit authorization. Neither credentials nor successful server responses are
invented. The original HttpBackend completion callback at `0x401bf22c` (adjusted
`0x401bf234`) genuinely returns without work; the tick consumes its HTTP result.

The request machine retains the original endpoint names, methods, request order,
128-byte URI limit, 128-byte request payload, 4096-byte HttpBackend response and
1024-byte ZBackend response. MIME matching is exact. The original BrewPen list
parser repeatedly writes a single local URI/GUID slot, and its command path
forces index zero; this behavior is retained.

Azure stores parsed names and quantities in an owned pending command. Session
commands carry the original group flag. The service preserves the 30-second
session window, unsigned deadline comparisons, repeated timeout enqueue attempts,
one-slot mailbox behavior, and the existing retry implementation. MQTT operations
are typed, required service calls. A missing or failed operation returns
`MissingService` or `ServiceFailure`, including callbacks invoked by MQTT. The
service does not claim an MQTT transport has been supplied by the SDK: the pinned
Arduino dependencies do not include that client. Application composition must
bind `AcquireClient`, `BindClient`, `SetServer`, `SetCallback`, `Connect`,
`Subscribe`, `Loop`, `Publish`, `Connected` and `Disconnect` to an actual client.

`AzureService::setup` prepares the original username, subscription, publication
and SAS fields. The shared console is registered explicitly through
`register_azure_console`, as are the other service consoles. Information and
legacy on-device test commands require real caller-supplied callbacks. The Azure
`send` command calls the recovered state publication path. Names, search text,
authorization and hook contexts passed as pointers must remain alive while their
requests or registrations use them; parsed command payloads are owned.

## Original execution comparison

`backend-services-validation.json` records **732** deterministic scenarios:
**730 exact results** and **two explicitly characterized memory-failure cases**.
`tests/fixtures/backend-services-original.json.gz` is deterministic gzip with
`mtime=0`. `tests/backend_services_cases.py` regenerates every input. The native
probe links the production modules and pinned ArduinoJson, with HTTP calls
replaced at the same explicit API boundary used by the original execution.

The two Ghidra scripts execute original instruction bodies through return. Every
reached returning assertion calls the real `0x400db014`, including the global
counter increment, clock-dependent continuation and 200-ms delay. The tests cover:

- All session types, program quantities, bounded serialization and parsed-prefix
  recovery; create/end requests and session log pump/valve/RSSI/timing fields.
- HttpBackend requests, configuration/request failures, registration/list/reference
  JSON, full tick dispatch and completion, loading events and HTTP errors.
- Azure state, connection, retry, session-window and recovery combinations;
  ordinary and invalid command/session messages, firmware rejection, quantities,
  enum assertions and group-session ownership.
- SAS input bytes, base64/URL escaping, truncation, empty device/expiry assertions,
  and HMAC failure propagation. The HMAC fixture replaces the cryptographic
  primitive, not the recovered SAS assembly; the target build separately compiles
  the concrete mbedTLS adapter.
- Exact names, tokens, descriptions and match modes emitted by the four original
  console registration bodies. Generic Shell allocation/registration is a boundary
  here and is independently compared in the console evidence.

The instruction executor models reviewed ROM memory/string/format operations,
compiler floating-point helpers, clock/log sinks, HTTP/MQTT APIs, cryptographic
primitives and mailbox calls. Diagnostic comparison records selected semantic
source lines; formatted output, UART traffic and live cloud behavior are outside
this comparison. Full recipe-build compatibility remains described by the
existing recipe evidence: deliberate malformed-input rejections do not become
successful backend results. These new cases do not claim complete original
recipe-builder execution for every downloaded recipe.

### OEM ten-digit session-ID defect

The original endpoint `0x40111984` uses a decimal buffer at `SP+2` immediately
before its stack canary at `SP+12` in an `ENTRY 0x30` frame. Writing a positive
10-digit ID and its terminating NUL overwrites the first canary byte. With the
reviewed nonzero-low-byte guard, the original reaches the real `0x401367a0`
stack-check failure body and then the SDK abort boundary `0x4008d7b4`.
The SDK `stack_check.o` seeds the whole guard from `esp_random`; its low byte is
not guaranteed to be zero.

Cases `backend-59` and `edge-2` preserve that original terminal result. A
nine-digit control and a ten-digit guard-low-byte-zero control return normally.
The reconstructed endpoint uses a 16-byte decimal buffer and returns its bounded
URI. This is an intentional memory-safety difference, **not full equality with
the original execution**. The report consequently sets `all_results_match` to
false and verifies both exact cases and the documented difference separately.

## Reproduction and limits

Prepare inputs with `tools/validate_backend_services.py --prepare-input DIR`.
Run `EmulateBackendJson.java` and `EmulateAzure.java` on a private copy of the
preserved Ghidra project with the prepared JSON and output JSONL paths. Pass the
result directory and preserved/expanded instruction exports to
`tools/validate_backend_services.py --original-directory DIR --export EXPORT
--export CALLBACK_EXPORT --write-evidence`. Native replay is also run by
`tests/test_backend_services.py`.

The pinned ESP32 compiler builds these sources and the HMAC adapter into the
recovered object archive. This validates target compilation and dependency use;
it does not link or flash a complete application, exercise live cloud services,
or establish hardware parity. Those application/transport/hardware boundaries
remain explicit in the integration inventory.
