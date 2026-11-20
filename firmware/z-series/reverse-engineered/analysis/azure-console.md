# Azure live integration and password diagnostics

The reconstruction now implements both original diagnostic entry points:
`0x400fec74` tests the supplied live Azure service, and `0x400fa684` checks
the two original development-host signature vectors. The SDK password command
delegates to the portable helper. The application console binds the separate
live integration entry through its existing diagnostic dispatch.

`azure-console-validation.json` records 40 complete original/native
comparisons, 2,341 ordered events, 32 missing or rejected service checks,
72 AddressSanitizer/UndefinedBehaviorSanitizer cases, and strict compilation
of the production service, diagnostic helper and concrete SDK adapter with
the pinned ESP32 GCC 5.2 toolchain. The compressed original fixture is
deterministic (`mtime=0`). Its compressed and uncompressed bytes, execution
inputs, original image, tools and reconstructed source identities are bound
in the report.

## Recovered behavior

The live diagnostic constructs a temporary development-host Azure service
for the signature check. Its remaining four TestScopes act on the supplied
live service: disconnect, reconnect, enter `SessionLoaded`, then discard the
session. Each wait executes the original state predicate, actual service
tick, original unsigned absolute timeout comparison and watchdog reset.
The original timeout assertion returns; the caller still performs its state
assertion and completes the TestScope. The reconstruction preserves that
continuation.

The signature helper is now separate from full SAS password construction.
It performs the original HMAC, Base64 and URL-escape sequence and preserves
the caller's previous output if HMAC fails. The two diagnostic vectors reuse
one output buffer, so the second crypto failure can leave the first
signature visible. Full password creation retains its separate failure
diagnostic. The production service also emits the original transition and
backoff messages through the supplied diagnostic service.

The scenarios cover all initial live states, an invalid initial state,
already disabled or disconnected transports, client-factory unavailability,
failed MQTT connection, a transport that ignores disconnect, retry timings,
stack thresholds, assertion-counter wrap, clock wrap, a specifically zero
session-loaded deadline, alternate hosts, incorrect key material and failed
or corrupted HMAC results. Assertions update the real original counter and
perform the original delay. Both original TestScope constructor and
destructor bodies execute, including their stack check and result logging.

## Execution and privacy boundaries

The original instruction replay executes 37 original function bodies,
including the two complete diagnostics, signature routine, Azure lifecycle,
backoff, wait predicates, TestScopes and returning assertion helper. It
verifies the hash-pinned image mapping before executing those bodies.

Cryptography is an explicit SDK boundary: original execution uses Java JCE
HMAC-SHA256 over the actual image key and input bytes; native execution uses
OpenSSL HMAC-SHA256 independently. The original Base64 call, unused temporary
MQTT constructor, MQTT/socket operations, clock, watchdog, stack query,
formatted output and ROM memory operations are explicit boundaries. The
temporary native Azure owner does not construct an unused MQTT connection;
all live transport actions still occur on the supplied service. MQTT
delivery is not part of these diagnostics, and the native fixture explicitly
fails if it unexpectedly invokes the unrelated message parser.

Key material, credential-bearing strings, signatures, assertion text and
formatted diagnostic bytes are hashed before any public trace is written.
The key is recovered only into the ignored build header from the pinned
OEM image. Five independent byte/encoding checks reject its appearance in
the published sources and fixture. No original backend request is sent.

Callbacks that are missing or reject an operation return an explicit source
failure. Those dependency failures are source guards, separate from an OEM
assertion that reports an error and then continues. The older backend
services replay was rerun after signature factoring; its documented malformed
input differences and explicit environment limits remain unchanged.

This evidence establishes the tested instruction and service behavior. It
does not establish physical installation, a functioning retired Azure
service, actual network timing, complete CPU emulation or hardware safety.
LeakSanitizer is unavailable in the ptrace-managed execution environment;
AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

## Reproduction

Native CI requires a C++11 compiler and the OpenSSL development package
(`libssl-dev` on Ubuntu). Run:

```sh
python3 -m unittest discover \
  -s firmware/z-series/reverse-engineered/tests \
  -p test_azure_console.py -v
```

To regenerate the original evidence, use a separate preserved Ghidra project
and the pinned Ghidra/JDK tools:

```sh
python3 firmware/z-series/reverse-engineered/tests/run_azure_console_emulation.py \
  --project "$PROJECT" --ghidra-home "$GHIDRA" --java-home "$JDK" \
  --output "$EXECUTION"
python3 firmware/z-series/reverse-engineered/tools/validate_azure_console.py \
  --execution "$EXECUTION" --toolchain "$TOOLCHAIN" --arduino "$ARDUINO" \
  --write-fixture --write-report --sanitize
```

The runner uses a read-only project, disables analysis and JVM performance
data, and copies its scripts into an isolated temporary directory. The
execution directory must be new. Without `--execution`, the validator
compares against the committed compressed original fixture.
