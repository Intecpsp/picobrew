# Live HTTP diagnostic reconstruction

The portable `http_client_console_test` implements the complete Z119 callback
at `0x40110f80`. It uses a dedicated `HttpServices` transport supplied by the
application, the same production `HttpClient` implementation, and the original
diagnostic environment through `ApplicationConsoleHooks`.

The callback first checks network-client acquisition, then executes the eight
original tests: HTTP GET, three HTTPS GET variants, HTTPS POST, invalid protocol,
HTTP connection failure, and HTTPS connection failure. Each test retains its
own `TestScope`, source line, and assertions. The response Stream retains its
headers, byte count, completion flag, optional body output, and returning
assertions. Its ordinary debug scopes remain distinct from `TestScope`.

The wait helper calls yield, reads the clock, and ticks the real asynchronous
client until it returns to Idle. The seven inter-test gaps execute a 100 ms
delay followed by the watchdog reset. Initial/final stack measurements, timing
dumps, the Wi-Fi-specific `EHOSTUNREACH` checks, and the final Idle assertion
also execute. Assertions that successfully reach their diagnostic service
continue execution, matching the OEM helper. Missing services and callbacks
that report failure return an explicit `ConsoleResult`; failure from the
otherwise void body/completion callbacks is retained and propagated.

## Original-execution evidence

`http-client-console-validation.json` records 55 complete original/native
comparisons and 48,696 matching ordered events. The Ghidra replay executes 56
original function bodies, including the complete top-level test, constructor,
request parser, state machine, response Stream, ordinary scopes, `TestScope`,
wait helpers, destructor, and assertion helper. The runs execute 511 returning
assertions and 780 watchdog resets.

Cases include Ethernet and Wi-Fi selection, missing initial clients, failed
per-test acquisition, connection errors, wrong errno, unreachable hosts that
unexpectedly connect, short writes, partial reads, timeouts, error HTTP status,
missing/incorrect content length, early connection closure, low heap, low
stack, and wrapping clock/assertion counters. Every socket operation and every
formatted diagnostic byte sequence is compared in order. Diagnostic timestamp
double bits are compared independently of host floating-point formatting.

The native suite also checks 12 missing dependencies and 27 rejected callbacks.
All 94 original-input/contract scenarios pass AddressSanitizer and
UndefinedBehaviorSanitizer. LeakSanitizer cannot operate in this ptrace-managed
execution environment and is explicitly excluded from that claim. The new
module compiles with warnings treated as errors using the pinned ESP32 GCC
5.2.0 toolchain.

## Private resources and reproducibility

The original test contains token-bearing URLs and assertions. Committed source
uses only their original numeric addresses. `recover_backend_credentials.py`
checks the preserved image hash and materializes the bytes into the existing
ignored, mode-0600 `oem_credentials.hpp`; the callback consumes the generated
`http_client_test_text` lookup. Unknown addresses return null. No synthetic
endpoint or default credential is substituted.

The original replay hashes all formatted messages, socket text, and request
bytes before writing output. The native comparison hashes the corresponding
bytes in memory. The public fixture contains those digests, explicit synthetic
socket inputs, and execution metadata. It is split into deterministic gzip
shards, each below 80,000 bytes, to keep publication payloads bounded. The index,
every shard, original inputs, scripts, sources, and target object are bound by
SHA-256 in the validation report. A separate check confirms that the original
authorization and the four live endpoint resources do not occur in committed
source or decoded proof data.

Run the committed proof without Ghidra:

```sh
python3 -m unittest discover -s firmware/z-series/reverse-engineered/tests -p test_http_client_console.py -v
```

The replay runner accepts an isolated copy of the hash-pinned original Ghidra
project through `--project`, plus the pinned `--ghidra-home`, `--java-home`, and
a fresh `--output` directory. It opens the original image read-only and disables
analysis. `validate_http_client_console.py --execution OUTPUT --toolchain TC
--write-fixture --write-report --sanitize` reproduces the comparison, target
compile, and native fault/sanitizer evidence.

Socket/network acquisition, clock, stack-water readings, watchdog, delay,
formatted output, compiler arithmetic, and ROM memory primitives remain
explicit execution boundaries. These tests make no live OEM network requests
and do not claim physical Wi-Fi, TLS, watchdog, or hardware qualification.
