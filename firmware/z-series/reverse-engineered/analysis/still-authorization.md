# Z119 PicoStill authorization service

`StillAuthorization` implements the original Still backend at primary PicoStill
`+0xa68` / Core `+0x5480`. The primary PicoStill object starts at Core `+0x4a18`;
its public interface is the four-byte-adjusted pointer at Core `+0x4a1c`.
The authorization record is Core `+0x4b6c`, and the pending byte is Core `+0x5490`.

## Recovered behavior

| Body | Reconstructed operation |
| --- | --- |
| `0x4011a228` | Clear the authorization record; assert and continue if already pending; construct the bounded StillRequest URL; configure an explicitly supplied Wi-Fi TLS client; set authorization; serialize and POST the request. |
| `0x40119d84` | Serialize MachineUID, MachineType=2, PicoStillUID and HasCleanedAck into the bounded 512-byte request span. |
| `0x400effcc` / adjusted `0x400f004c` | Reset response storage and accept either exact JSON MIME spelling, with signed Content-Length at most 512 (including unknown -1). |
| `0x401118bc` / adjusted `0x401118d0` | Append response bytes with original frozen/null assertions, overflow warnings and truncation behavior. |
| `0x401bf1bc` / adjusted `0x401bf1c4` | The original completion callback genuinely returns without additional work. |
| `0x401164ac` | Once pending and the shared HTTP client becomes Idle, parse the response and clear pending. HTTP ticking is owned by the PicoStill controller. |
| `0x401163a8` | Clear the result, parse PicoStill.UID, ProfileID and CleanedAckDate, retain bounded owned values and emit the JSON error on failure. |

The controller's `authorized` observation is **UID length nonzero**, and its
`cleaned` observation is **CleanedAckDate length nonzero**. ProfileID is retained
as a signed integer; it does not determine either boolean. UID capacity is 16
characters, and each date capacity is 32. Neither successful HTTP status nor a
synthetic true value can substitute for the original response parser.

`StillAuthorizationBinding` owns a real `WiFiClientSecure` and changes the shared
HTTP transport socket only after configuration succeeds. The original request
always supplies client class 3; it does not use the general Ethernet/Wi-Fi factory.
The normal StillClient separately owns/selects its class-2 WiFiClient. Both clients
share the asynchronous HTTP engine and have lifetime-stable receiver contexts.
The application installs the historical HTTP authorization string from its
verified configuration. No credential is copied into validation fixtures.

## Evidence

`still-authorization-validation.json` records **332 of 332 exact comparisons** of
the original instruction bodies with the production C++ modules and selected
ArduinoJson adapter. Cases cover pending assertions and continuation, configured
and requested failure results, null and long request fields, complete request
JSON, all MIME/size acceptance branches, body freezing/truncation, null spans,
JSON type conversions, missing fields, malformed/unsupported/deep JSON, every HTTP
lifecycle state and full request/header/body/completion/tick sequences.

The original assertion helper `0x400db014` actually runs, including its diagnostic
counter update and 200-ms delay. The fixture records assertion file/line/expression,
semantic diagnostic source lines, clock and counter, ordered HTTP calls, request
bytes, buffer lengths/flags, record fields and controller observations.
`Z119StillExecution.java` differs from the common helper only in its class name
and larger instruction/nesting budgets needed to execute long serialization and
the original nested JSON parser. The common helper is unchanged. HTTP methods,
ROM memory/string calls, compiler arithmetic, clock and output sinks remain
explicit comparison boundaries. Not every formatted verbose log is compared.

`still-authorization-target-build.json` records the three owned target objects,
compiled with the pinned historical ESP32 toolchain and SDK. Native negative tests
also verify absent authorization, absent/failing socket selection, missing HTTP
services and failed diagnostics/assertions produce explicit failure. No complete
application link, live service or physical hardware execution is claimed here.
