# Z119 Wi-Fi manager

`wifi.hpp` and `wifi.cpp` recover the original `Wifi.h` application state
machine. The separate ESP32 adapter calls the evidenced Arduino ESP32 1.0.1
radio and DNS APIs. Core event delivery and console registration are required
application bindings; neither has a successful default implementation.

This is part of the **OEM reconstruction**. It retains the original access
point name/password and DNS test hostname, including behavior a later standalone
port should replace deliberately. Its presence does not establish a complete
network backend or an installable Z firmware image.

## Original instruction boundaries

| Address | Recovered operation |
| --- | --- |
| `0x400f6ae8` | Setup: disable Wi-Fi persistence, mode off, deprecated auto-connect call, MAC read, console registration |
| `0x400f220c` | State transition and ordered radio, address, credential and event effects |
| `0x400f27e0` | Main tick, scan/connection progression, timeouts and RSSI updates |
| `0x400f2738` | Failure handling, retry scheduling, Idle transition and error event |
| `0x400d5bc0` | Select target and stop the retry timer |
| `0x400e6b84` | Require a nonempty SSID, copy requested credentials, select Connected |
| `0x400d782c`, `0x400d77c4`, `0x400e1820`, `0x400d5b74`, `0x401bf884` | Backoff start, scheduling, expired retry, stop and expiration test |
| `0x400d0f1c`, `0x400d0e60`, `0x401bf254` | Credential/string comparison |
| `0x400ef214`, `0x400ef180`, `0x400ee998` | Reset credentials and bounded string storage |

The object begins at Core `+0x3d1c`. Its state pointer is `+0x14`, target
is `+0x24`, state date is `+0x20`, retry timer is `+0x28`, and the six-byte
MAC field is `+0x44`. The original Core constructor at `0x40120a04` supplies
500 ms power-up, 1,000 ms disconnect and 20,000 ms connect delays. Retry
durations start at 60,000 ms and double to 480,000 ms.

The state object starts at Core `+0xf38`: ID, RSSI and signal level occupy
its first twelve bytes; requested and connected credentials are at `+0xc`
and `+0x90`. Their third field is the original `IsInternetExpected` flag.
The access-point address is 192.168.4.1. Local, gateway and DNS test addresses
remain distinct, with the original little-endian IPv4 word representation.

## Behavior preserved

The ordinary connection sequence is Idle, PoweringUp, Scanning,
ScanCompleted, Connecting, Checking and Connected. One transition happens
per tick. Scanning waits only while the SDK reports `-1`; another negative
result becomes a completed scan with zero reported networks. Completed scan
counts retain the original signed 16-bit narrowing.

A Connected target is retained through the scan. A scan-only target becomes
ScanCompleted and waits there; an access-point target starts the original AP
after scanning. Changing a Connected target or any requested credential
forces Disconnecting. Every actual state transition clears the previous
connected-credential snapshot; entering Connected copies the requested values
back into it and stops backoff.

The original SDK RSSI result is narrowed to a signed byte. Level zero covers
nonnegative values; levels four, three and two start at -49, -59 and -69 dBm;
lower values use level one. All 256 byte patterns are compared with original
execution. Disconnecting clears signal level, and loss detected by the
Connected tick also clears RSSI.

The DNS probe remains `www.picobrew.com`. Checking succeeds only when
`hostByName` returns exactly one. The subsequent state decision uses the
recorded result even when `IsInternetExpected` is false. This is observed
original behavior, not an assumption that Internet access is appropriate for
the standalone port. The original setup calls the deprecated `setAutoConnect`
method, whose historical SDK implementation returns false; it does not call
`setAutoReconnect`.

Time comparisons intentionally retain the OEM rules. State delays use
`now > state_date + delay`, with unsigned addition; retry expiration uses
`deadline <= now`. These are not replaced with wrap-safe elapsed subtraction.
Retry start resets the attempt counter, schedules the initial duration, then
doubles the next duration. Later failures advance an already expired timer.
An unexpired timer preserves the original returning assertion path.

Diagnostics are excluded from the owned state machine, including original
credential logging. Native `assertion_line` records application assertion
sites for comparison. A missing or failed required service returns an error;
an SDK operation returning zero is a separate, preserved application input.

## Validation

All **1,240 scenarios** match execution of **17 original bodies**, including
state, target, credentials, addresses, timers, assertion lines, and ordered
radio/event calls. The portable suite retains **1,048 original cases**; 192
additional deterministic generated scenarios were checked during acceptance.
The complete input, original output, source and instruction identities are in
[`wifi-validation.json`](wifi-validation.json).

The instruction harness uses the preserved Z119 image and the reviewed
Xtensa register-window helper. Arduino radio, DNS and IPAddress operations,
event sinks, diagnostic output and console registration are explicit execution
boundaries. It does not claim to execute RF hardware, the SDK network stack,
FreeRTOS scheduling or the complete application. Some injected SDK results
exercise defensive/narrowing paths beyond ordinary successful SDK returns.

A separate native integration test follows connection and disconnection over
multiple ticks, checks retry deadlines, and delivers events into the real
Core single-slot mailbox. It also checks missing/failed services. Address and
undefined-behavior sanitizers pass; local LeakSanitizer is disabled because
this executor is traced and cannot run leak inspection.

The owned manager and concrete Wi-Fi adapter compile with the pinned Xtensa
GCC 5.2 toolchain and historical Arduino SDK, alongside the existing modules.
[`wifi-target-validation.json`](wifi-target-validation.json) records their
object/source/header identities. Target compilation is not a full firmware
link, boot, radio, recovery or physical-brewing qualification.

```sh
python3 tools/validate_wifi.py --prepare-input build/wifi/input.json
# With the preserved Z119 Ghidra project, execute EmulateWifi.java read-only.
# Arguments: absolute input JSON and original-output JSONL paths.
python3 tools/validate_wifi.py --original-jsonl build/wifi/original.jsonl \
  --export <expanded-original-export> --write-evidence
python3 -m unittest discover -s tests -p test_wifi.py
```

The ESP32 `WifiBindings` object and its manager must remain alive while the
returned services are used. Application composition must provide the console
registry and event sink; a full mailbox follows the established first-event
wins rule. Credential ingress must preserve the OEM bounded-string invariant:
SSID and password arrays contain a terminating NUL before any SDK call.
Core service assembly, HTTP/cloud backends, remaining Head/UI
services, and the custom standalone network contract remain separate work.
