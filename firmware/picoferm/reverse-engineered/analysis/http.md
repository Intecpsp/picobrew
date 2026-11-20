# PicoFerm HTTP setup service

The owned HTTP implementation reconstructs the inbound PicoFerm 0.2.7 service
from image SHA-256 `08c34af891036494ae81d6f29e7495a475f67be977aa4effd3adde6fee4e9532`.
It supplies complete parser, form, page and route behavior and a concrete
Arduino ESP8266 client/DNS adapter. The application owns DNS lifecycle and
enables this service while its access point is active.

## Original functions and ownership

| Original address | Recovered implementation |
| --- | --- |
| `0x40203394`, `0x402033c8`, `0x4020da7c` | Nullable string equality/prefix and non-NUL character search |
| `0x402034e4` | Bounded form URL decoding, including original malformed-hex semantics |
| `0x40203410`, `0x40204b2c` | Request URI extraction and complete blocking HTTP parser |
| `0x402064ec`, `0x40206924`, `0x402069d4` | Wi-Fi form and case-insensitive action trigger parsing/response |
| `0x40205f84`, `0x40202f14` | Common response header, HTML prefix and flush/yield footer |
| `0x40206074`, `0x40206284`, `0x40206338` | Wi-Fi setup, factory reset and pressure calibration forms |
| `0x402063dc`, `0x40206608` | Stored measurement details and Wi-Fi registration response |
| `0x402079c0` | DNS processing, enabled gate, connection parsing and all seven routes |
| `0x402035d0`, `0x40204cf8` | Original CString and request-line embedded self-test vectors |

`HttpServer` owns its request, Wi-Fi form and trigger buffers. It references the
application's actual `DeviceData` and `PersistentData`; successful `/setSsid`
requests call the application configuration service, which updates those same
objects and marks credentials changed. Reset and calibration actions run only
after successful trigger validation, completed response writes, flush, yield
and the original 2,000 ms delay.

The constructor's request timeout is 1,000 ms. Original `0x40204e00` adds
`0xa00` to the application pointer at `0x4020509d`, loads `0x3e8` at
`0x402050a0`, and stores it at `+0xec` at `0x402050a3`. The resulting
application field `+0xaec` is HTTP field `+0x390`.

`poll()` processes one DNS request and yields before checking `enabled`.
Disabling HTTP suppresses client acceptance. DNS initialization, start and
stop remain application effects; the concrete listener exposes the original
TTL/reply-code/IP parameters through `set_dns_ttl`, `set_dns_reply_code`,
`start_dns` and `stop_dns`.

## Exact browser output

`assets/http-page-prefix.html` contains all 4,272 original bytes at
`0x4024170c`, including embedded CSS, JavaScript and image data. The extractor
verifies the OEM image identity and generates `src/http_assets.inc` directly.
It does not rely on the decompiler's truncated string literal. ESP8266 page
constants stay in flash and use `WiFiClient::write_P`; native tests compare
the same bytes through an explicit flash-text boundary.

The remaining pages retain their original text, CRLF/LF placement, links,
unescaped scan SSIDs, physical log indices, fixed two-decimal measurement
formatting and response headers. The routes are `/`, `/factoryReset`,
`/calibrate`, `/details`, `/setSsid`, `/factoryResetComplete` and
`/calibrateComplete`. Unknown paths return the original HTTP/1.0 404 body.

## Comparison evidence

`http-validation.json` records 440 complete original-versus-native cases.
They compare every owned URI/body/form/trigger byte, content length,
header-completion flag, first error, complete response bytes, the entire
236-byte persistent record and 112-byte device-data region, credentials
changed flag, consumed stream bytes, clock result and ordered service events.

The original replay executes the actual string, URL-decoder, request-line,
blocking parser, page, route, credential mutation and embedded self-test
bodies. It also executes the actual newlib integer parser, SDK fixed-decimal
formatter and both returning diagnostic helpers. Assertions and warnings
execute their two original SDK yields and return. SDK DNS/client/Wi-Fi/String,
Print transport writes, scripted clock/delay, calibration queries and final
device actions are named boundary models. The existing Data, Hardware and
Core oracle helpers and their shared instruction engine are reused unchanged.

Cases include header overflow and continuation, incomplete/disconnected
requests, content-length overflow and signs, repeated/case-sensitive headers,
deadline refresh and unsigned clock wrap, encoded NUL/high bytes, malformed
hex, stale error and form state, signed scan counts, wrapped measurement logs,
all seven routes, failed action triggers and deliberate trigger truncation.
Three native tests verify this comparison, exact extracted assets and the
separate bounded rejection policies.

## Deliberate reconstruction boundaries

The original request-line copy can overwrite the URI field when it exceeds
255 bytes. The original body writer saturates at byte 511 and can remove its
NUL terminator. Those cases are explicitly rejected by the owned parser,
while preserving bounded buffers. They are tested as reconstructed behavior
and are not labeled successful OEM parity cases. Null/zero-capacity decoder
destinations also receive an explicit rejection rather than an invalid write.

The details page uses larger temporary strings than the original 8-byte
pressure formatter destination. Normal stored measurements are compared;
extreme outputs that overwrite original stack memory and invalid persistent
log indices are outside successful returning comparison evidence.

`http-target-build.json` records three objects compiled with the pinned
Xtensa LX106 GCC 4.8.2 / Arduino ESP8266 2.4.1 dependencies, including the real
DNS/client adapter. Whole-application linking, physical Wi-Fi behavior,
pressure calibration and installation qualification are separate evidence.
