# Zymatic 1.1.14 network interfaces

The Zymatic runs an AVR application with two independent network drivers. On either interface the OEM application **initiates** HTTP connections to fetch the user's menu and post session data. The Hub implements the corresponding server endpoints. A second, inbound, read-only device API in the modified firmware uses the wired interface. Addresses below are byte addresses in the decoded application or SRAM data-space addresses as indicated. [Original image and provenance](../../oem/README.md) · [Hub stock protocol handler](../../../../server/internal/server/stock_zymatic.go)

## Outbound brewer requests

Startup copies initialized data from flash `0x1b24a` into SRAM starting at `0x0100`. The two network paths use distinct pointers to the same hostname text.

| Transport | Hostname in flash → SRAM | HTTP Host construction | DNS lookup | TCP destination |
|---|---|---|---|---|
| Wi-Fi | `0x1b297` → `0x014d` | `0x2eaa` | `0x4d08`, `0x9a82` | 12 service calls to `0x11562`, TCP 80 |
| Ethernet | `0x1b9d0` → `0x0886` | `0xa734` | `0x1032c`, `0x1037c` | 12 service calls to `0x18580`, TCP 80 |

These references support redirecting the original protocol to a local Hub, including HTTP Host and DNS. Separate from the service calls, the original Wi-Fi connection diagnostic at `0x98c2` uses a fixed external IPv4 address and port 80. Its outcome alone cannot establish Hub reachability. The OEM request strings cover user setup/sync, firmware checks, session logs and session recovery. They carry the original protocol's parameters. A USB-provisioned REST key belongs only to the new **incoming** API.

The Wi-Fi DNS call can be followed through `0x114cc`, `0x1213c` and `0x144ca`. At `0x1454a` the driver builds and sends opcode `0x1010`, then waits on the same opcode at `0x14556`. This matches the TI CC3000 host driver's hostname lookup. The OEM HCI code shares command/event and buffer state. The corresponding `r24:r25` command sequences include socket creation at `0x149e8`, connect at `0x14484`, select at `0x1483c` and close at `0x1492c`. No matching bind, listen, accept or setsockopt command wrappers were identified. A separate Wi-Fi server would need these operations, event arbitration with the original driver, management of both accepted and outbound descriptors and measured upper bounds on every transaction during a brew. The current inbound server uses Ethernet. [TI/Adafruit CC3000 HCI opcodes](https://github.com/adafruit/Adafruit_CC3000_Library/blob/master/utility/hci.h) · [Adafruit CC3000 server implementation](https://github.com/adafruit/Adafruit_CC3000_Library/blob/master/Adafruit_CC3000_Server.cpp)

## Wired interface

Routine `0x10252` initializes the wired connection through `0x181ba`, configures retries and resolves the service host. The callers at `0x103dc`, `0x107f4` and `0x11024` store its return value into SRAM `0x230b`. The original interface-selection path explicitly clears it at `0x1056e`. The adjacent Wi-Fi flag has its own lifecycle.

The Ethernet register read at `0x19060` and write at `0x19212` select the peripheral, transmit opcodes `0x0f` and `0xf0` respectively, clock the 16-bit big-endian address and a data byte, then release the peripheral. The OEM board-level selects are PORTD bit 6 active-low and Arduino logical pin 15 active-high. The firmware pin tables map pin 15 to PORTD bit 7. Socket registers start at `(4 + socket) << 8`. Receive buffer pointer masking in `0x18fea` uses 2 KiB per socket. The TCP allocator beginning at `0x18580` considers socket states CLOSED, FIN_WAIT, and CLOSE_WAIT reusable. The UDP allocator at `0x18b34` also scans four sockets. These details match the W5100 register map. [WIZnet chip documentation](https://docs.wiznet.io/Product/Chip/Ethernet/W5100) · [Arduino Ethernet driver](https://github.com/arduino-libraries/Ethernet/blob/master/src/utility/w5100.cpp)

The modified interface claims socket 3 only when closed, the OEM wired-selection flag is set, the existing SPI configuration is usable, both select lines are released and all sockets have the expected 2 KiB rings. It defers to an OEM connection already using that socket and checks the port/mode before continuing an accepted request. A foreground service call transfers at most 16 request or response bytes, with interrupts masked for one four-byte SPI transaction at a time. A hardware test is still needed with real DHCP, concurrent recipe sync, OLED refresh, and a running brew. [Wired transport](../../modified/zymatic_api_w5100.c)

## REST observations

For `SyncUser` the OEM Wi-Fi path allocates a 13-byte stack buffer at `0x8f4a`. It calls the machine serial reader at `0x8f52` and sends that buffer after `&machine=` at `0x9090`. The reader at `0x4342` calls hardware enumeration and the formatter at `0x2ddc`, copies the 12-character serial, and terminates it before returning success. The extension observes successful OEM calls, validates the serial and caches it for the authenticated status field `stock_serial`. The field is absent until a successful original read. The separate `device_uid` identifies the private Hub enrollment.

The authenticated status endpoint reads the OEM wort and thermoblock float samples at SRAM `0x23dc` and `0x23e0` with an atomic four-byte load. The OLED mirror transposes the original 128 × 64 frame at SRAM `[0x266a,0x2a6a)` from row-major, MSB-first pixels into the Hub's page format. Each 128-byte response page has a fingerprint of the entire drawing buffer. A changed fingerprint returns HTTP 409 so the Hub can restart the read. A status response served from the OEM program interpreter's button-poll site reports `busy=true` and `state=program_running`. Other foreground sites report `state=monitoring`. A display poll inside that interpreter returns HTTP 503 with `display_busy` before it samples the frame, preserving the budget for physical controls. The display resumes after the program exits. The panel may redraw after a client has received a snapshot. The new API exposes no actuator commands. [OEM control evidence](../tests/stock-controls.cjs) · [Device API](../../modified/zymatic_api.c)

## Reproduce

Supply the original ZIP or its extracted HEX to check the hostname references, connect calls, interface selection, SPI operations, and socket allocator landmarks. The native test drives a W5100 register model through the actual firmware HTTP implementation. It exercises API authentication, circular hardware buffers, timeouts, foreign sockets, display frame conversion and revision changes. Neither check requires a USB connection.

```sh
python3 firmware/zymatic/reverse-engineered/network/verify.py /path/to/Zymatic_1_1_14.zip
python3 -m unittest discover -s firmware/zymatic/modified/tests -p test_zymatic_api.py
```
