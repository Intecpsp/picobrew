# Z119 MQTT protocol and Azure network binding

The MQTT implementation is reconstructed directly from the preserved Z119 image
`zseries_0_0_119.bin`, SHA-256
`d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27`.
It belongs to the PubSubClient family. An exact upstream revision has not been
established, so this implementation is pinned to the original instruction bodies,
not an assumed library release.

## Recovered implementation

| Source | Original behavior |
| --- | --- |
| `src/mqtt_client.cpp` | `0x40126d8c` CONNECT and CONNACK, `0x40126f6c` loop, `0x401270fc` publish, `0x4012718c` subscribe, `0x40126b90` packet parser, `0x40126ccc` packet writer, `0x40126d48` disconnect and `0x401bff2c` connected. |
| `platform/esp32/azure_mqtt_platform.cpp` | Concrete Azure effect bindings to an owned MQTT client and a distinct `NetworkClientBinding`; uses actual Wi-Fi, Ethernet and TLS Client methods. |

The wire protocol is MQTT 3.1.1 with a 1024-byte buffer and 15-second keepalive and
socket timeout. The implementation preserves CONNECT flags and string order,
CONNACK state codes, packet identifiers and wrap, retained publish, QoS 0/1
subscriptions, QoS 1 incoming acknowledgements, binary payloads, strict keepalive
deadlines, timeout handling, connection-loss flushing and stopping, and complete
draining of oversized packets. Azure uses the original non-Stream path; the
optional library Stream interface is outside this implementation.

Transport callbacks report actual requested bytes, returned counts and connection
state. Missing callbacks return `MissingService`; malformed spans and oversized
writes have explicit results. Callback failures and custom Ethernet transport
failures propagate through the Azure adapter instead of becoming successful
operations. The adapter owns a separate client factory, preventing an HTTP request
from replacing the active MQTT socket. HMAC uses the existing real mbedTLS binding
and application-supplied key bytes; no live cloud credentials appear in fixtures.

The adapter also supplies Azure information and legacy HMAC-test console callbacks,
exact assertion expressions and selected diagnostic messages. Those platform
callbacks were recovered by source inspection and target compilation. They are
not included in the MQTT binary-execution equivalence claim. In particular, the
legacy HMAC callback checks the original expected signatures through the recovered
password assembly and the configured key; an absent required key is a service
failure rather than a passing test.

## Original execution comparison

`mqtt-client-validation.json` records **252 of 252 exact results**. The deterministic
gzip fixture compares full packet-buffer bytes, protocol state, timestamps,
identifiers, ordered socket effects and message callback contents. It covers TCP
failure, every CONNACK code 0 through 5, all credential and will combinations,
partial writes, subscribe/publish boundaries, timeout and wrap cases, keepalive
request/response and timeout, incoming QoS 0/1/2 messages, retained and binary
payloads, malformed remaining-length prefixes, and oversized-packet draining.

`EmulateMqttClient.java` executes the original function bodies through return.
`Z119MqttExecution.java` is a dedicated copy of the reviewed execution helper with
explicit Xtensa hardware LOOP setup/backedge control and an increased instruction
budget for bounded oversized-packet draining. The CONNECT protocol-name copy uses
that real original loop. The common helper used by other evidence is unchanged.
Socket methods, the clock and ROM memory/string operations are explicit boundaries;
this is not a complete Xtensa/SDK or network simulation.

Two memory-safety cases are deliberately outside exact-equivalence claims: the
reconstruction rejects unchecked overlong CONNECT strings before opening a socket,
and it rejects malformed incoming topic/payload spans before exposing memory to a
callback. Native tests check these guards and missing-service propagation. These
improvements must not be described as original-firmware results.

## Target and limits

`mqtt-client-target-build.json` records the pinned GCC 5.2 / Arduino ESP32 1.0.1
build of both owned objects, with dependency and source hashes. The wider build
also compiles the selected Ethernet2 dependency and the actual TLS adapters.
No complete application link, live Azure interaction or physical-network/hardware
execution is claimed here. Application-level credentials and lifecycle composition
remain the responsibility of the Core owner.
