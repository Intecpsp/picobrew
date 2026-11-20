# Incoming local HTTP parser and static resources

`include/zseries/local_http.hpp`, `src/local_http.cpp` and
`src/http_assets.cpp` recover the incoming request-line/header parser and
static route body from the preserved Z 0.0.119 image, SHA-256
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.
The source owns the parser state, bounded path storage and exact embedded
web resources. It has no runtime dependency on the original binary.

This is a bounded application slice. TCP accept/read/timing, line framing,
POST recipe dispatch, live-state serialization and outgoing HTTP exchanges
remain separate work. No complete Z application or installable image is
produced, and no physical machine was tested.

## Original behavior

| Address | Recovered responsibility |
| --- | --- |
| `0x400eeffc` | Reset method, content length, JSON flag, path and parser completion flags |
| `0x40109b4c` | Parse method and path from the request line |
| `0x40109c34` | Select request-line parsing or recognize content type/content length headers |
| `0x400ee998`, `0x40100830`, `0x401007f0` | Reset and copy the path through its original 64-byte buffer |
| `0x4011b0e4` | Select the default document, ping, live-state service, static resource or 404 |
| `0x4012a76c`, `0x4012a734` | Forward the ping string to the Print virtual write service |

The original comparison also executes the string comparison, prefix and
delimiter-search helpers. `analysis/local-http-validation.json` identifies
all executed bodies, instruction-export hashes, source hashes and service
boundaries.

The parser deliberately retains the observed behavior:

- Method names use case-sensitive prefix matching against `Unknown`, `GET`,
  `POST`, `PUT`, `DELETE` and `UPDATE`; for example, `GETTING` selects GET.
  Missing delimiters can retain a previous method or path.
- The path is the nonempty token between the first two spaces. It truncates
  after 64 bytes, remains NUL-terminated and records overflow. There is no
  URL decoding, query removal or HTTP version validation.
- An empty or null consumed line marks headers done, even before the first
  request line. Further supplied lines are still processed after that flag
  is set; the transport is responsible for choosing when to stop.
- Only the exact case-sensitive prefix `Content-Type: application/json`
  sets the JSON flag. Suffixes are accepted and a subsequent non-JSON type
  does not clear it.
- Only `Content-Length: ` with that capitalization and space invokes the
  decimal conversion service. Later matching headers overwrite the prior
  length. Signed results are stored in the original unsigned 32-bit field.

The decimal conversion is an explicit boundary for original ROM `atoi` at
`0x400566c4`. The comparison covers its defined signed-32-bit result domain,
including leading whitespace, signs, trailing text and nonnumeric input.
It does not claim to recover overflow behavior outside that domain.

The static router chooses `/z.html` for an empty path or `/`, returns `PONG!`
without a newline for `/ping`, calls the live-state service for `/state`,
and otherwise searches the eight original resource descriptors. Its asset
comparison skips the first path byte without checking for a slash:
`xhelp.html` selects `help.html`, while `help.html` does not. Unknown paths
emit a 404 header with zero length. Query strings and case variations do
not match the assets.

The route body itself does not examine the method. The original server
caller dispatches POST separately to `0x401172b8`; direct route-body tests
with POST are not evidence that a real POST request follows the GET path.

## Exact embedded assets

The eight records at `0x3f4426a4` contain name, MIME type, size and data
pointers. `tools/extract_http_assets.py` reads those records directly from
the hash-verified original image. It emits the raw files in `assets/http/`,
readable C++ literals and `analysis/http-assets.json`. UTF-8 BOMs and CRLF
bytes are preserved. No credentials or machine-specific settings are
introduced.

| Resource | MIME type | Bytes |
| --- | --- | ---: |
| `help.html` | `text/html` | 4,862 |
| `lib.js` | `text/javascript` | 1,250 |
| `z.css` | `text/css` | 2,858 |
| `z.html` | `text/html` | 1,903 |
| `z.js` | `text/javascript` | 9,282 |
| `zstate.css` | `text/css` | 555 |
| `zstate.html` | `text/html` | 830 |
| `zstate.js` | `text/javascript` | 1,183 |

## Service ownership and failures

`LocalHttpParserServices::decimal_integer` supplies the ROM/library integer
conversion. `LocalHttpResponseServices::headers`, `write` and `state` supply
the original response header formatter, byte transport and live machine
state serializer. The implementation has no invented `/state` JSON body.

Missing or failed services return explicit errors from the callable
reconstruction API. A missing body-write service is detected before
headers are sent; a failed header write prevents the body write. These
guards are host-tested additions to the integration boundary, not claims
about original firmware behavior with an absent SDK. Parser reset preserves
the service binding and clears the request state.

## Validation and reproduction

All **341 original-instruction scenarios** match the readable native source.
They compare parser snapshots after every supplied line, ordered response
events and SHA-256 of every emitted body. The corpus includes method-prefix
and delimiter cases, path lengths through 2,047 bytes around the 64-byte
boundary, both parser logging states, duplicate and malformed headers,
signed-32-bit conversion limits, resets, all eight resources and route
variants across every method plus an invalid method value.

The Ghidra harness executes the original parser, buffer, route and string
helper instruction bodies. ROM memory/string routines, integer conversion,
diagnostics, header output, live-state output and the virtual byte writer
are explicit services. The virtual writer reads bytes from original mapped
memory; expected asset content does not come from the reconstructed source.
The portable tests compare against the resulting checked-in goldens and
independently reread the original image for asset identity.

```sh
python3 tools/extract_http_assets.py --check
python3 -m unittest discover -s tests -p test_local_http.py -v
python3 tools/validate_local_http.py --prepare-input /tmp/z-http-input.json
```

To reproduce the original execution, use the existing Z119 mapped Ghidra
project and the script directory `tools/ghidra`:

```sh
analyzeHeadless <project-directory> firmware \
  -process zseries_0_0_119.bin -noanalysis -readOnly \
  -scriptPath <this-directory>/tools/ghidra \
  -postScript EmulateLocalHttp.java /tmp/z-http-input.json /tmp/z-http-original.jsonl
python3 tools/validate_local_http.py \
  --original-jsonl /tmp/z-http-original.jsonl \
  --export <preserved-instruction-export-directory> --write-evidence
```

The existing `tools/build_esp32_objects.py` includes both new C++ modules
automatically. They compiled with pinned Xtensa GCC 5.2.0 and Arduino ESP32
1.0.1 using warnings as errors. `analysis/local-http-target-build.json`
records those two objects, source/header hashes, compiler identity and
flags. This establishes target compilation, not a linked application,
real socket integration or hardware behavior.
