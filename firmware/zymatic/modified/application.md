# Zymatic standalone firmware

Both versions use the brewing controller from PicoBrew 1.1.14. The Hub chooses the version that fits the verified application space on each brewer. **About Zymatic** shows the installed version.

| Feature | 1.2.0 | 1.3.0 |
| --- | --- | --- |
| Stock recipe menu and physical dial | Yes | Yes |
| Sync recipes and sessions with a configured Hub over Ethernet or Wi-Fi | Yes | Yes |
| One USB-provisioned recipe available offline | Yes | Yes |
| Device website over Ethernet | Status and display with device key | Public temperatures and state, dial pairing and authenticated controls |
| Hub front panel | Read the idle display | Mirror the OLED and send a dial movement or press when the current screen accepts it |
| Brew history on the machine | Hub polling while connected | Eight retained session headers and a shared pool of 222 time-stamped samples |
| Firmware installation and stock restoration | USB | USB on a unit with a verified boot window |

The device and Hub websites use the original menu for brew control. Turn and press act on the displayed selection. The machine rejects input if the screen has changed, the frame is stale, a fault is active, or the current screen requires local input. During a program, the remote dial can press the original button where the firmware permits it. Pairing approval always uses the physical dial. [Controller evidence](../reverse-engineered/README.md)

History records the measurements the Zymatic actually supplies. Interrupted sessions stay available after reboot. The Hub can import them when Ethernet reconnects. [Recorded channels and retention](history.md)

## Pair a brewer

Connect the Zymatic to Ethernet and enter its address in **Add machine**. With 1.3.0, compare the code in the Hub with the one on the brewer’s **About Zymatic** screen and approve it at the physical dial. The Hub stores the key. A paired owner can use **Manage machine → Open machine website**. Stock Wi-Fi supports outbound recipe and session traffic. The custom incoming API and device website use the wired W5100. [Original network protocols](../reverse-engineered/network/README.md)

## Installation and recovery

First-time USB setup uses 1.2.0, which fits below every supported boot boundary. USB verification reads the full flash twice. A backup of the stock application or an Optiboot filename alone cannot establish how much space a particular brewer reserves for its bootloader.

To qualify an expanded application window, capture the configured fuse and lock bits through the external programmer on the same provisioned brewer. The installer checks the prior paired UID and compares the live boot bytes against the independent capture before choosing an image. Both the private API key and identity carry forward. Stock restoration clears the recognized custom application extent and verifies the full flash after writing. [Boot evidence and recovery](recovery.md) · [USB setup](README.md)

The private reference package builder defaults to 1.2.0. To compose a 1.3.0 image for simulator testing, use `tools/package.py --firmware-version 1.3.0`. Its manifest reports the required boot boundary. Keep personalized images in private storage. A generic HEX updater does not perform the Hub’s fit, identity or recovery checks.

CI compares the reconstructed OEM control paths and executes the overlay, screen handling, EEPROM recorder and packed OEM assets in an AVR simulator. Physical brewing, power-loss recovery on a real unit and installation of an expanded application still need hardware trials. Network firmware installation remains unavailable. [1.2.0 build and memory details](application-1.2.md) · [Offline recipe format](offline-recipe.md)
