# Incoming HTTP lifecycle, framing and response headers

`local_http_server.hpp` and `local_http_server.cpp` reconstruct listener enable
(`0x400d5c54`), connection polling and request framing (`0x4011fc58`), and exact
HTTP/1.0 header formatting (`0x400d97c4`). They compose the previously recovered
`LocalHttpParser` and static router. The reference is the preserved Z119 image
with SHA-256 `d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.

## Observed state machine

An enable change first stops the listener, then starts it if the new flag is
true. An unchanged flag performs no socket operation. This method does not
explicitly close an accepted client. The Core constructor configures port 80,
four listener clients and a 1,000 ms request deadline.

When enabled, a tick checks whether the current client is connected. If it
is not, the server assigns the nonblocking accept result and checks again.
A successful accept resets the parser, request-complete flag and 2,048-byte
buffer, then sets `deadline = now + timeout`. Reading starts on the next tick.

An existing connection closes when `deadline < now`. Equality permits
reading. The unsigned addition and direct comparison retain the original
wrap behavior; there is no sliding deadline after each byte.

Each read byte other than NUL appends to the bounded buffer. An invalid read
result, including -1, closes the connection without a response. Before headers
finish, LF triggers removal of trailing TAB, LF, CR and space. Vertical tab and
form feed are not trimmed. The resulting line is consumed and the buffer is
reset. An overlong line records overflow but still parses its retained prefix.

At the end of headers, zero content length completes the request immediately.
Lengths above 2,047 receive an empty 400 response and close. Other requests
collect the declared number of non-NUL body bytes across ticks. Surplus bytes
remain unread. POST invokes the application handler; other methods invoke the
static router. A completed response closes the client.

## Headers and boundaries

The formatter emits `HTTP/1.0`, numeric status, and `OK`, `BAD REQUEST`,
`NOT FOUND` or `ERROR`. Nonnegative lengths emit `Content-Length`; a nonempty
MIME type emits `Content-Type`. The response ends with
`Connection: close\r\n\r\n`. Tests compare exact original output bytes,
including signed integer printing and omitted fields.

`LocalHttpTransport` binds accept, client connection/read/close, listener
start/stop and byte output. It separates adapter completion from whether an
accept found a connection. POST and live-state serialization are application
bindings. Their concrete implementations compose this state machine; the
framing oracle records the bytes delivered to POST without pretending to
execute recipe construction at that boundary.

Missing or failed services return integration errors. Completed requests still
close if their handler fails. A connection that vanishes between the original
two connection checks, or an attempt to append to a frozen buffer, returns a
failure under an intentional bounded integration policy. The OEM assertion
helper logs, increments its diagnostic counter, delays 200 ms and returns.
After the frozen-buffer assertion, the OEM rechecks the frozen flag, skips
the append and continues request framing. The reconstructed guards return
earlier rather than reproduce these continuations. They are tested separately
and are outside the 169 successful original comparisons below.

## Evidence

All **169 original-instruction cases** match native snapshots, ordered service
trace hashes, POST payload hashes and response-byte hashes. Coverage includes
accept deferral, partial lines and bodies, ignored NULs, all method dispatch
families, exact/expired/wrapped deadlines, line overflow, the 2,047-byte body
limit, invalid reads, unread trailing requests, enable transitions and 75
status/length/MIME formatter combinations.

The original buffer, parser, route and Print helpers execute in the Ghidra
harness. Explicit boundaries are Wi-Fi/socket SDK operations, ROM memory and
integer conversion, diagnostic output, and the separately reconstructed POST
and live-state applications. The tests establish software behavior, not a
concurrent SDK, a TCP network or physical hardware.

```sh
python3 -m unittest discover -s tests -p test_local_http_server.py -v
python3 tools/validate_local_http_server.py --prepare-input /tmp/z-server-input.json
analyzeHeadless <project-directory> firmware \
  -process zseries_0_0_119.bin -noanalysis -readOnly \
  -scriptPath <this-directory>/tools/ghidra \
  -postScript EmulateLocalHttpServer.java /tmp/z-server-input.json /tmp/z-server-original.jsonl
python3 tools/validate_local_http_server.py \
  --original-jsonl /tmp/z-server-original.jsonl \
  --export <preserved-instruction-export-directory> --write-evidence
```

`analysis/local-http-server-validation.json` records source and instruction
hashes. `analysis/local-http-server-target-build.json` records pinned Xtensa
GCC 5.2 compilation of the module. A target object is not a complete linked
application or installable Z image.
