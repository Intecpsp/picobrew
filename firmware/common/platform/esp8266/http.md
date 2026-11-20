# ESP8266 foreground HTTP listener

`HttpListener` adapts the unchanged common `PicoHttpServer` to the raw lwIP API
in the pinned ESP8266 Arduino 2.4.1 SDK. Its constructor performs no hardware or
network I/O. The application calls `begin(80)` after station connectivity exists
and polls the common transport in its normal foreground loop.

The listener owns one connection. It retains at most one received pbuf chain,
acknowledges bytes only as foreground code consumes them, and returns `ERR_MEM`
for another chain until capacity becomes available. The SDK retains ownership
of refused data. Generation-tagged descriptors prevent a stale close from
affecting the next connection. All callbacks are detached before an owned PCB is
closed or aborted; a `tcp_err` callback never dereferences the PCB already freed
by lwIP.

Each receive/send transfers at most the common 512-byte I/O budget. Outgoing
`tcp_write` uses `TCP_WRITE_FLAG_COPY`; accepted bytes are reported once even if
the following `tcp_output` must retry. Send-buffer exhaustion, transient output
backpressure and pending input return immediately. A received FIN closes only
the peer's sending half, so a complete HTTP request can still receive its
response. The common transport owns request/response deadlines, parsing,
authentication, asset streaming and bounded response memory.

The native test compiles this adapter against the actual pinned SDK headers and
uses controlled raw-API callbacks to exercise partial chains, backpressure,
half-close, read/write/output failures, connection rejection, stale handles,
callback detachment and the real authenticated HTTP parser. It checks every
allocated mock PCB and pbuf is released. ASan and UBSan cover these paths;
LeakSanitizer's process tracer is unavailable in the managed executor and is
disabled there. The full PicoFerm modified SDK link independently requires the
actual listener and common HTTP symbols.

```sh
ESP8266_ARDUINO=/path/to/esp8266-2.4.1 \
  python3 firmware/common/tests/test_esp8266_http.py
```

These callback tests and the target link do not establish physical network,
heap, throughput or radio behavior. The checked revision and exact boundaries
are recorded in [the validation report](../../analysis/esp8266-http-validation.json).
