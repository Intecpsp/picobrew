# Z119 StillClient and sensor protocol

`still_client.*` recovers the PicoStill command client and HTTP response
callbacks. `pico_still_sensors.*` recovers the sensor CSV decoder, sample
derivatives, timed snapshots and rolling statistics. The existing
`PicoStillController` can call this client directly and consume its observation
snapshot. The separately reconstructed [outgoing HTTP engine](http-client.md)
now supplies that transport interface through `still_http.*`, with completion
and application return values kept separate. Application socket acquisition
and complete backend assembly remain explicit dependencies.

## Original mapping

| Entry | Recovered operation |
| --- | --- |
| `0x400f83f0` | Set and validate the 16-byte Still host |
| `0x400f947c` | Set pending command and construct the command URI |
| `0x400f0174` | Reset pending/error/completion/header state and reset HTTP |
| `0x4010c8d0` | Cancel an outstanding request, reset and send a command GET |
| `0x4010ded0`, `0x400e1180` | Configure request and the original form encoder |
| `0x400daeb4`, `0x401bf8d8` | Propagate HTTP/socket errors and retry eligibility |
| `0x400efee0`, `0x4010dfc4`, `0x40100f4c` | Headers, body accumulation and response completion |
| `0x401009bc`, `0x400db7bc` | Sensor record decoder and comma iterator |
| `0x400d5730` | Five sample derivatives per minute |
| `0x400ef9cc`, `0x400db708` | 10/60-second samples and statistics publication |
| `0x400ef818`, `0x400d0df8` | 30-sample rolling statistics and difference filter |
| `0x401bf4c8`, `0x400e7078` | Shared Still error predicate and error-text priority |

The original StillClient fields include the host at `+0x38`, URI at `+0x3d8`,
error/retry flags at `+0x468/+0x469`, pending command at `+0x46c`, response headers
at `+0x470`, 256-byte body at `+0x4c8` and completion flag at `+0x5d8`. The shared
Still state contains live sensors at `+0x10`, 10/60-second sensors at
`+0xc0/+0x170` and T1/T2/pressure statistics at `+0x220/+0x350/+0x480`.

## Request and response semantics

All twelve command labels use the preserved enum table. Requests are
`http://<host>/<command>` with the original plain-client class value two.
Configure sends `ssid=<encoded>&pass=<encoded>` as
`application/x-www-form-urlencoded`. A new request cancels a nonempty pending
command before reset. Reset clears headers and completion/error flags but does
not clear the prior response body; the headers callback clears the body.

HTTP error ten and socket errors 104, 113 or 128 are retryable. Response
completion sets error for every HTTP status other than 200 and retryable only
for 404. It then verifies that the retained body has at least three bytes and
begins/ends with `#`. IP and version payloads are copied to a 32-byte buffer and
then have their final retained character removed. Consequently an overlong
payload leaves 31 bytes, matching the original copy-then-remove order. Valid
payloads are processed even when the HTTP status itself indicates failure.

The original form encoder escapes punctuation in four ASCII ranges, carriage
return and newline. It emits uppercase hexadecimal without two-digit padding;
its cursor still advances by three bytes. Newline therefore produces an
embedded terminator after `%A`. It also requires an extra byte of spare capacity
before accepting an escape. These unusual details are preserved and checked for
every nonzero byte value. Plain text that fills the destination truncates with
success, as in the original.

The OEM Configure caller ignores encoder failure and can pass an unterminated
stack buffer to string append. That undefined read is not reproduced: the owned
client returns `InvalidCredentialsEncoding` before submitting an invalid form.
The standalone encoder retains the original partial writes and error values.
Null body data with nonzero length similarly returns `InvalidBody` after
recording the original returning assertion line.

## Sensor and runtime behavior

A Data record contains four Fahrenheit temperatures, pressure, three flags and
error text. Temperatures are converted with `(value - 32) / 1.8`; pressure is
retained. Original comparisons use the NUL-terminated, at-most-256-byte
record domain supplied by StillClient's body buffer. The public parser also
accepts an exact nonterminated span: token searches and error copying are
bounded by its length, with embedded NUL bytes retaining the OEM logical end.
This removes the original CString representation precondition for direct
callers; the sanitizer test checks a nonterminated record and all its prefixes.
Each numeric/flag token must have fewer than 16 bytes. The original
zero check compares whether `"0.0"` starts with the token, so `""`, `"0"` and
`"0."` are accepted zero spellings while `"0.00"` is rejected. Nonzero decimal
prefixes retain `atof` behavior. A flag is true when its token is nonempty and
its first byte differs from `0`; it is not parsed as a numeric or JSON boolean.
Error text `None` is cleared. The parser handles the complete remaining error
string, including any commas, then removes its final `#`.

Before validating a sample, the parser clears timestamp, derivative interval
and error text, retaining previous numeric values, flags and derivatives. A
failed sensor parse does not itself change the client's HTTP success flag.
The original completion path still calls statistics update; returning
assertions expose missing/stale sensor timestamps. This behavior is retained
as OEM reconstruction, rather than silently changing the machine's protocol.

Derivatives use unsigned 32-bit timestamp subtraction, divide by 1000 and scale
each value change by 60. The original zero-interval behavior, including IEEE
infinities, is retained. Timed snapshots update only when their prior timestamp
plus 10,000/60,000 is strictly less than `now`, with the same unsigned wrap.
The three rolling windows retain 30 values, sum, mean and summed squared
deviations. Each also records the difference from the replaced slot and applies
the original alpha 0.05 exponential filter. Error text prefers connectivity
failure, then the sensor error, then the general Error-state label.

## Independent evidence and integration

`EmulateStillClient.java` executes the preserved original instructions, including
the original bounded-string, iterator and statistics bodies. Only HTTP exchange,
ROM memory operations, compiler arithmetic, decimal `atof`, percent-hex
formatting and diagnostics are modeled boundaries. The libc boundary is
explicit: this validates application-level parsing over the recorded decimal
domain, not all of Newlib's accepted numeric syntax.

All **4,186** cases match native source across **50 original bodies**. The
portable compressed fixture retains **4,058** cases; 128 independently executed
seeded numeric cases are regenerated by the acceptance tool. Comparisons include
full text bytes, all state/header/error fields, ordered HTTP calls, all sensor
double bits, entire circular windows and their derived values. Coverage includes
every nonzero encoder byte at eight capacities, request failure combinations,
all command/status combinations, malformed records, token/string limits,
every tested flag combination, timestamp wrap, full-window replacement and
error-text precedence. Provenance and source identities are recorded in
`still-client-validation.json`.

The sanitizer integration test binds the recovered controller to this client,
delivers split HTTP bodies and exercises start, live sensor publication and
Stop. It also checks preserved failure/status quirks and the explicit undefined
input repairs. The 35-module historical ESP32 archive compiles the added source;
`still-client-target-validation.json` records the compiler/SDK/source/object
identities. No live socket, Still hardware, complete application link or boot
qualification is implied.

```sh
python3 tools/validate_still_client.py --prepare-input /tmp/still-client-input.json
# Execute EmulateStillClient.java in the mapped Z119 Ghidra project with
# -noanalysis -readOnly and the input/output JSON paths.
python3 tools/validate_still_client.py \
  --original-jsonl /tmp/still-client-original.jsonl \
  --export /path/to/z119-callback-export --write-evidence
ASAN_OPTIONS=detect_leaks=0 python3 -m unittest discover -s tests -p test_still_client.py
```
