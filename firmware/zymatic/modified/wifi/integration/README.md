# Zymatic foreground Wi-Fi scheduling

The experimental compact and extended images start the listener from the original six-item Home menu after the stock Wi-Fi setup has associated and acquired an address. Home shares the HTTP parser with the Ethernet service. The scheduler selects a single transport and discards a previous transport's client state when the interface changes.

When the operator chooses a menu item, the scheduler keeps that choice while a pending radio transaction finishes. The original action runs once, with the stock driver in control of the radio. The extended image also serves the local website during the physical About pairing prompt. Before the next About screen queries the network, it passes the radio back to the stock driver. Normal brewing and its outbound session traffic use the stock driver.

If the radio stalls, Home can continue with the commissioned offline recipe. The normal service checks take their disconnected paths, and opening Settings shows “Restart for network” for the unavailable connection. The private heap object stays allocated and stock retains the physical dial. A normal access-point disconnect is handled separately by the radio driver, which waits for association and DHCP.

## Verification

From this directory, run the scheduler tests with the host's C compiler.

```sh
python3 verify_native.py
```

The instruction tests use the project's pinned Arduino AVR compiler, the original 1.1.14 ZIP or HEX, Node.js, and the dependencies in `firmware/zymatic/reverse-engineered/tests`.

```sh
python3 verify_avr.py --toolchain /path/to/avr/bin --oem-file /path/to/Zymatic_1_1_14.zip
```

The tests execute all six original Home selection paths, track the W5100 register boundary, compare the stock service branches and check access to a saved nine-step recipe after radio loss. A second test uses the compiled radio driver, models the CC3000's SPI pins, and stalls a byte after the HTTP port bind request. These are off-device checks. The full firmware layouts have separate size, packaging, and simulation tests.
