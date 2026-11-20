# Outgoing HTTP client reconstruction

`http_client.*` recovers the original URI/request object, synchronous
GET/POST/PUT request writer and six-state response engine. `still_http.*`
connects it to the recovered StillClient and PicoStill controller, including
asynchronous socket-error propagation before the controller's ClientTick.
`platform/esp32/http_client_platform.*` performs actual historical Arduino
`Client` operations. Application-owned socket acquisition and the original
Ethernet client's TLS preparation remain required bindings.

This is a source module checkpoint. It does not link a complete application,
run a network transaction on ESP32 hardware, produce an installable image or
establish TLS certificate/connection behavior.

## Original entry points

All addresses refer to the preserved Z 0.0.119 image, SHA-256
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.

| Original address | Recovered behavior |
| --- | --- |
| `0x400eff54`, `0x4010c65c`, `0x400d149c` | Request reset, URI parsing and validity |
| `0x400eff84`, `0x400f01cc` | Constructor defaults: ports 80/443 and both timeouts 5,000 ms |
| `0x400f0124`, `0x4010c7ec`, `0x40110e2c` | Client reset, configuration and authorization header |
| `0x400db188`, `0x400db2c8`, `0x400db324` | State transition, cancellation and error replacement |
| `0x400f90d8`, `0x400f912c`, `0x400f9238` | GET, POST and PUT wrappers |
| `0x400f8f9c`, `0x400f8a74`, `0x400f0088` | Send guards, socket connection, request serialization and header-buffer flush |
| `0x40101bc4` | Response polling, header parsing, body delivery and completion |
| `0x400d5664`, `0x400db3b4`, `0x400db108` | Send/reply/body timing |
| `0x401c0018` | Historical SDK dotted-decimal IPv4 parser; original instructions also executed |

The HTTP object has its state/error/socket-error at `+0x20/+0x2c/+0x30`,
the request at `+0x38`, response at `+0x3b8`, received byte count at `+0x410`,
receiver at `+0x414`, a 1,024-character buffer at `+0x418`, and timing fields
at `+0x828`. Request host/path/authorization capacities are 64/256/512;
response content type has capacity 64. Owned source uses explicit fields and
does not depend on copying original pointer layouts onto a host ABI.

## Preserved request behavior

Only lowercase `http://` and `https://` are recognized. Host or path truncation
invalidates the request; authorization truncation is recorded while accepting
the header. An absent slash produces an empty path, so the historical wire
form is `GET  HTTP/1.1` with two spaces. No slash is inserted by this module.
Port parsing accepts a signed decimal prefix and narrows to 16 bits. Literal
addresses use the SDK's original parser, including its acceptance of empty
octets; address zero falls through to the host-name connect overload.

The original URI parser searches for a colon across the whole URI tail. A
colon occurring after the first slash creates an invalid source-span length
in the original. The owned parser rejects this undefined case before copying
the host. The sanitizer integration exercises that repair separately from
the original-defined comparison domain. Public URI, authorization and content
type inputs are NUL-terminated C strings; public fixed arrays must retain
their terminators. Payloads instead use explicit byte lengths.

The sender checks its state, error, client and request, then obtains free heap,
prepares the Ethernet client when applicable, and connects by address or host.
Only connection result one is successful. Heap thresholds and class/scheme
mismatches remain observable returning assertions. Each request emits two
header-buffer writes: request line and Host first, then optional Content-Type
and Authorization, Content-Length, Cache-Control and Connection. Content-Length
uses the original seven printable character limit. Header short writes fail;
the original payload-write return value is ignored. Flush follows the payload.

## Response states and original limitations

| State | Polling behavior |
| --- | --- |
| Idle | No polling; entering Idle stops and releases the bound socket |
| Initialized | Request configured; sending starts the reply deadline |
| WaitingReply | Any nonzero availability advances to Headers; a strictly expired deadline records error 10 and returns to Idle |
| Headers | Read one byte at a time, renew the header deadline after availability, trim trailing tab/LF/CR/space, parse exact-case fields and call the receiver at the empty line |
| Body | Read up to 1,024 available bytes and deliver the requested count; advance when declared length is reached or the connection closes with no available data |
| Complete | Call the receiver's completion callback on the following tick, then return to Idle |

The parser recognizes `HTTP/1.1 `, `HTTP/1.0 `, `Content-Length: ` and
`Content-Type: ` with their original case. Header status other than 200 stops
the exchange before the receiver sees headers. This matters to StillClient:
its isolated response callback can classify a 404 as retryable, but this
transport rejects that status first. The integration test preserves that
distinction. If the connection is already closed after an error, premature
close can replace the first error and record the original assertion.

There is no body inactivity timeout, chunked decoding or redirect handling.
A body can complete after early connection closure even if its length is less
than Content-Length. The `readBytes` result is ignored and the requested byte
count is delivered, matching the original Stream boundary. A header timeout
sets its error while remaining in Headers until the next tick. These are
recovered behaviors, not claims of a hardened HTTP implementation.

## Platform and application assembly

`HttpServices` distinguishes a service operation's completion from its SDK
return value. Missing operations return `MissingService` or `ServiceFailure`;
they never imply a successful connection. Socket write/read/connect/available,
flush/stop/connected, heap, errno and clock operations have concrete historical
Arduino bindings. Caller-owned `HttpBindings` and socket lifetime must cover
the exchange. An existing WiFiClient or WiFiClientSecure can be supplied with
its explicit class. Automatic factory selection requires `acquire`; the
Ethernet class additionally requires `prepare_ethernet` for its original
TLS/errno preparation. These application services are not implemented here.

`still_http_services()` supplies StillClient's reset/cancel/configure/GET/POST
operations and receiver callbacks. `tick_still_http()` must be used at the
controller's HttpTick stage so current HTTP state/errors are visible to the
subsequent ClientTick. Both clients and the binding must remain alive through
completion. The controller, StillClient and sensor decoder are the real
recovered modules in the integration test; only the socket/clock and unrelated
controller service effects are fixtures.

## Independent evidence and reproduction

`tools/ghidra/EmulateHttpClient.java` executes **42 original bodies** with the
shared windowed-call execution harness. Unknown calls fail the run. Standard
libc memory/string/decimal-formatting operations, deterministic clock/heap,
the application socket factory, virtual Arduino socket/Stream operations,
receiver callbacks and diagnostics are explicit boundaries. ROM `strncpy`
address `0x400015d4` is identified in the pinned SDK linker map
`tools/sdk/ld/esp32.rom.spiram_incompatible_fns.ld`. Decimal conversion cases
stay within the documented signed 32-bit domain; the ROM conversion's complete
overflow implementation is not reconstructed.

All **933 original/native cases** match, including full buffer bytes,
request/response fields, error/assertion state, dates/deadlines, return values
and ordered socket/receiver events. The portable compressed fixture retains
**837** deterministic cases; 96 seeded cases are also checked in the original
replay. Capacity cases begin with a pending header prefix read on earlier
ticks, then deliver the final byte, keeping each original invocation within
the shared instruction budget while exercising the exact 1,024-byte limit.
The acceptance report records image, instruction, input, output, harness,
source and fixture hashes. Constructor defaults are independently read from
their original instructions; the replay covers the mapped behavior closure.

The three host tests replay the retained original evidence, verify its source
identity, and run an ASan/UBSan transport integration. That integration splits
headers and bodies across polls, executes Start, sensor Data and Stop through
the real controller/client chain, and checks transport 404 rejection, body
inactivity, early close, strict reply deadlines, missing dependencies and the
malformed-colon span repair.

```sh
python3 tools/validate_http_client.py --prepare-input /tmp/http-client-input.json
JAVA_HOME=/path/to/jdk-25 /path/to/ghidra/support/analyzeHeadless \
  /path/to/project firmware -process zseries_0_0_119.bin -noanalysis -readOnly \
  -scriptPath tools/ghidra -postScript EmulateHttpClient.java \
  /tmp/http-client-input.json /tmp/http-client-original.jsonl
python3 tools/validate_http_client.py \
  --original-jsonl /tmp/http-client-original.jsonl \
  --export /path/to/z119-callback-export --write-evidence
ASAN_OPTIONS=detect_leaks=0 python3 -m unittest discover -s tests -p test_http_client.py -v
make all
```

`analysis/http-client-target-validation.json` records successful compilation
of this engine, bridge and concrete platform adapter in the **38-module**
isolated historical GCC 5.2 / Arduino ESP32 1.0.1 archive. The target build
uses the same hash-pinned dependencies and object-build command documented in
`platform/esp32/README.md`. It establishes target compilation, not a complete
application link or physical network operation.
