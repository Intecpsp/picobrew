# Zymatic OEM 1.1.14 reconstruction

This directory describes behavior recovered from the original ATmega1284P application. The executable control model and its differential tests cover the operations below. They do not form a complete replacement controller. The standalone build retains the original temperature controller, mechanical sequencing and operator interlocks. [Original image and provenance](../oem/README.md) · [Executable control model](stock-controls.cjs)

The original Intel HEX has SHA-256 `eb04c9dafdae032e7770b142f868246caf82eed4d80a0989552fdfd5dfee7b7c`. Decoding it yields 113,610 contiguous application bytes with SHA-256 `d6bff3e9c49c092a9ed825b91790c0adbc46be7112f3d79f26d9a30d28e0a4cd`. Code addresses below are **byte** addresses in that application. The AVR stores code pointers and return addresses in words. SRAM and EEPROM addresses below are in their respective address spaces.

## Startup and display

| OEM code | Recovered behavior |
| --- | --- |
| `0x230a` | CRT initializes the stack to `0x40ff`, copies initialized SRAM `[0x0100,0x0a80)` from flash `0x1b24a` and clears SRAM `[0x0a80,0x2c8a)`. |
| `0x1a7bc` | Main calls MCU initialization `0x19e76` and application setup `0x11168`. The foreground invokes `0x10660` and serial event processing `0x1a1c6`. The physical menu and running brew can block within the foreground. |
| `0x10fac`, `0x1067c` | Hardware, encoder, network and previously stored account setup. EEPROM bytes 155 and 156 equal to 1 and 14 select the existing 1.1.14 setup. A mismatch enters the setup/migration path. EEPROM reads/writes at `0x4862`/`0x495c` call the AVR EEPROM register primitives at `0x19484`/`0x19492`. |
| `0x7a74` | Factory integrity check reads four EEPROM bytes starting at `0x0ffa`, compares only the low two to its 16-bit sum. The loop adds the first 16-bit word of each four-byte group, using 16-bit LPM reads over two passes of low flash. Failure displays a memory-corruption message. |
| `0x15c9c`, `0x1604e`, `0x15e46` | Pixel write, clear and OLED transfer. Framebuffer is 1,024 bytes starting at `0x266a`. Pixel (x,y) occupies mask `0x80 >> (x % 8)` at offset `16*y + floor(x/8)`. OLED transfer reads that backing buffer and expands pairs of pixels. |
| `0x10594`, flash table `0x0117` | Six main menu items in order are Brew a Recipe, Clean/Rinse, Machine Setup, Help, About Zymatic and Delayed Start. Selection goes through `0x81f0` and its physical encoder/button loop. |

## Motion, temperature and interlocks

| OEM code | Recovered behavior |
| --- | --- |
| `0x28b4`, `0x754c` | Recipe locations 0 through 5 map to arm-stop indexes 0, 1, 3, 4, 5 and 6. All higher byte values return index zero in the mapper. The recipe runner intercepts location 6 as a pause before this helper. The movement routine uses the **machine's** stop table at `0x2419 + 2*index`, compares with current position at `0x2417`, then calls relative motion at `0x70de`. It can rehome if the resulting difference is at least 11 units. |
| `0x37c4`, `0xbdda` | The Step Filter check reads logical pin 23. LOW twice, separated by 5 ms, reports removal. The running brewer's filter handler calls this routine. |
| `0x3ab6`, `0xee9c` | Logical input 0 LOW produces code 9. Otherwise input 4 HIGH with 13 LOW produces code 17. The fault display identifies heat pump off and backup relay off while the heater is on. Pin numbers here are software identifiers. Circuit polarity at a specific board connector still requires electrical evidence. |
| `0x4c74` | With interrupts masked, drives logical outputs 4 and 24 LOW, waits 500 ms and drives 13 LOW. ORs the supplied error into SRAM `0x2334` and writes the combined mask to EEPROM byte 121. |
| Vector `0x0080` → `0x109fc` | Timer 3 compare A handles sampling and protection. Flag `0x089d` gates work. The sensor refresh becomes due after at least 7,001 ms. Other interlock checks execute between refreshes. The four IEEE binary32 Fahrenheit readings at `0x23dc`, `0x23e0`, `0x23e4` and `0x23e8` correspond to wort, primary heat, board and backup heat. The diagnostic screen at `0xc6de` and following blocks prints the respective readings. |
| `0x3954`, `0x37fc` | With input 13 asserted, a heat channel at least 50°F above wort on all three compared samples trips the difference check. Three consecutive rises of at least 28°F trip the rise check. In the backup rise comparison, `0x38d6` loads the current **primary** heat reading for the final difference. Threshold equality and the historical asymmetry are covered by replay. |
| `0x10b22` | Strictly above 214°F wort or 225°F primary heat sets `0x230d`, clears `0x233b`, drives logical 4 LOW, waits 500 ms and drives 13 LOW. The exact equality cases are included in CPU regression. |

## Recipe storage, routing and interrupted brews

The network parsers at `0x8c1a` and `0xd680` stage the menu in SRAM. Ethernet recognizes the enclosing `#` and a record-ending `|`, then stops after ten recipes. `0x1ec5` holds the recipe count. An empty completed response can set that byte to 12 as a sentinel. The startup zero-fill clears the menu on reset.

Each record occupies 172 bytes starting at `0x16be + 172*index`. Bytes 0–20 hold its NUL-terminated name. Byte 21 gives its step count. The 25 six-byte step slots each contain a 16-bit SRAM pointer to a step name, followed by four unsigned bytes for target °F, duration in minutes, route and drain minutes. `0x2952` extracts wire fields, and `0x894e` parses step names and numbers. Identifiers are in separate 33-byte slots at `0x1d76`. Restoring these raw records alone after reset would lose the strings addressed by their pointers.

“Brew a Recipe” calls the sync/menu loader at `0x105fe`. The chooser at `0xfd4e` builds names from the current SRAM bank. Its ordinary physical selection dispatches the selected recipe to `0xf6fe` at `0xffea`. The normal route reads the step's location at `0xf94c`. A location of 6 displays the brew-program pause, with user choices, and turns outputs off. Other locations reach the arm-position mapper and the per-step interpreter `0xe648` through `0xfc10`–`0xfc2e`. At `0xfc32` the returned next step becomes the current index at `0x23c0`. The OEM compares that value with the stored count before continuing. The normal selection is the launch action. The physical menu offers separate Back and Re-Sync choices.

During a timed step, the original button read at `0xf488` leads to four physical choices. The actual dispatch sends Continue to `0xf644`, Exit Brew to `0xef12`, Next Step to `0xf67e`, and Drain/Pause to `0xf4a8`. A pass without a press goes to `0xf660`. Independent fault exits such as `0xef1c` occur earlier. The tests compare all five branch destinations on both images. A pause must have a following step because the prompt at `0xfa56`–`0xfaa6` reads that following step’s name even when the current step is the last one.

The EEPROM byte reader/writer wrappers have 22 and 27 direct callers, including the block access loops at `0x486e` and `0x496c`. The other call to the write primitive, at `0x4db4`, makes a complete address-space ownership assumption unsafe. Machine Setup’s Factory Reset at `0x10588` sets the migration markers to 2 and 15. The next startup invokes `0x4da4` and clears **every byte from 0 through 4095**, then records the application checksum through `0x4a3c`. The custom fallback record lives only in the provisioned flash image.

On entering a brew, `0xf732` calls the stock writer for the active flag at EEPROM byte 1. Stock session creation at `0xd5f6` saves the remote session identity in EEPROM bytes 122–153 and the following byte at 154. On interrupted startup, `0x10072` reads the identifier, requests the recipe from the server through `0xd886`, then fetches the reset point through `0xd03a`. Both successful results are required before `0x10156` calls the runner. Power-loss recovery of a stock remote brew depends on that service.

## Repeat the CPU checks

Run from the repository root. The first command prepares a private copy of the checksum-pinned stock HEX on the computer and uses no USB port. The tests require Node 20.19+ and npm 10+.

```sh
python3 firmware/zymatic/modified/tools/usb_stock.py --data /path/to/private/hub-data --action prepare
npm ci --prefix firmware/zymatic/reverse-engineered/tests --ignore-scripts
npm --prefix firmware/zymatic/reverse-engineered/tests test -- /path/to/private/hub-data/usb-stock/zymatic/Zymatic_1_1_14.hex
```

The suite checks HEX records and both SHA-256 values, then executes 494 AVR cases against the original instructions in AVR8js. It scripts digital I/O, elapsed waits, EEPROM writes and selected display/peripheral boundaries. It exercises the original floating-point and integer subroutines. To include the standalone parser, cold-start chooser and recovery cases, append the path to the privately built **raw decoded custom application** as a second test argument. For an image built with a selected recipe, append its normalized native program JSON (including ID) as the third argument. Placeholders in the example paths refer to your own files.

These checks do not measure real tank temperatures, arm calibration, physical button timing, interrupt latency, stack high-water under load or electrical output states. The brew controller and hardware still require bench qualification. [AVR8js project](https://github.com/wokwi/avr8js)
