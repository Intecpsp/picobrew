# Z119 network clients and Ethernet/TLS binding

`network_client.cpp` reconstructs the original `ZClient` owner and custom
`ESP32Ethernet` socket policy. `network_client_platform.cpp` supplies actual
historical Arduino `Client`, WiFi/WiFiSecure, W5500/DHCP/DNS and mbedTLS calls.
Each outgoing HTTP or MQTT connection needs its own `NetworkClientBinding`;
sharing one global factory would invalidate another service's live client.

## Executed evidence

`network-client-validation.json` records 1,087 original/native comparisons.
The actual factory acquire/clear, custom Ethernet connect/status/connected,
write/read/available/peek/flush/stop bodies execute through return. Assertions
execute `0x400db014`, including float timestamp conversion, counter increment,
200 ms delay and continuation. The fixture compares ownership, operation order,
return codes, data, deadlines and configuration. SDK allocation/client methods,
W5500 register/socket IO, mbedTLS IO and the handshake routine are explicit
boundaries. These comparisons do not claim to execute the cryptographic
handshake, DHCP/DNS packets or live network traffic.

The factory maps network 1 to Ethernet and network 2 to WiFi, with a separate
WiFiSecure instance when requested. It clears Ethernet, WiFi and WiFiSecure in
that order before constructing a different class. Ethernet receives a 30,000 ms
handshake timeout and cleared CA pointer; WiFiSecure receives 30 seconds. The
original only checks a newly constructed Ethernet client's connected state;
it checks both new and reused WiFi clients. Class/connected diagnostics return
and the original still supplies the handle. Invalid network IDs leave output
handles/classes zero and reject acquisition.

The socket code preserves all four original TCP failures: already owned `-30`,
no free socket `-31`, connect failure `-32`, and closed while waiting `-33`.
CLOSED, FIN_WAIT and CLOSE_WAIT sockets are reusable. The 16-bit source port
wraps to 1024. Stop waits at most 1,000 ms for closure, forces closure if needed,
clears the socket's server port, marks the socket unused, then stops TLS.
TLS writes retry WANT_READ/WANT_WRITE; ordinary reads preserve signed results.
The original raw receive length is signed 16-bit and send length unsigned
16-bit. A successful raw send returns the original requested length, including
its upper bits. The original `peek()` accesses the raw socket even with TLS.

## Concrete SDK binding

The original image embeds `ESP32Ethernet` paths. Its low-level W5500 framing,
eight sockets with 2 KB RX/TX each, 8 MHz/MSB-first/mode-0 SPI transactions,
software reset, buffer setup and socket algorithms correspond to the primary
[Adafruit Ethernet2 source](https://github.com/adafruit/Ethernet2/tree/0bab6349fb5b42fce2d5442bbed093279184b548).
This is an explicitly selected rebuild dependency, not a claim that its exact
revision was used by the vendor. `dependencies/ethernet2.json` pins every
acquired file and preserves license/provenance; `acquire_ethernet2.py` verifies
all bytes. Only listed files enter the adapted build directory.

`adapt_ethernet2.py` makes a separate, checked build copy. It passes the actual
System SPI object to the driver, avoids reinitializing that already configured
bus, and exposes DHCP timeout arguments. The original setup uses SPI bus 2,
pins 17/5/16, Ethernet CS21, the caller's DHCP deadline and a 5,000 ms response
timeout. This preserves coexistence with the display's SPI ownership. Device
identity is twelve lowercase hex digits in efuse low-byte-first order; the
Ethernet MAC copies WiFi's six bytes and increments its last byte by three.

The owned `EthernetTransport` implements the original custom TLS policy rather
than substituting the upstream plaintext EthernetClient. It uses the pinned
SDK's mbedTLS entropy/CTR-DRBG, `esp32-tls` personalization, client/stream defaults,
optional CA verification, hostname/SNI, BIO callbacks, handshake retry timeout,
10-tick RTOS waits, verification diagnostics, error propagation and context
release. The OEM factory clears the CA pointer; the reconstruction preserves
that OEM behavior. Standalone security-policy changes belong in `modified/`.
The TLS binding is source-reviewed against `0x40124760` and associated functions
and target-compiled; its internal handshake remains outside this instruction
comparison's scope.

## Explicit rebuild guards and limits

Missing/failed services and allocation failure report a distinct failure.
Null nonempty buffers are rejected. These guards are not attributed to the
original, which could dereference failed allocations or invalid spans. TLS
certificate contexts are also freed on failed setup/destruction to prevent
leaks; the original did not release them on every failure path. Heap-leak and
redundant-free debug messages are not reproduced. No credentials or OEM bearer
literal are added to source, and validation makes no cloud requests.

The target build compiles both the owned implementation and selected adapted
dependency. It is still object/archive evidence, not a complete application
link, a tested W5500 board, a physical brewing run or an installation package.
