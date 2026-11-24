# Zymatic Wi-Fi memory checks

The bench builds a compact and an extended Zymatic application from the verified OEM 1.1.14 image. It links the experimental Wi-Fi listener, checks the flash and SRAM limits and runs the existing firmware against a simulated AVR and Ethernet controller. The release installer uses the existing application profiles.

From the Zymatic `modified` directory, run the same check as CI. It uses the project's pinned Arduino AVR GCC 7.3.0-atmel3.6.1-arduino7 toolchain, simavr 1.8, a host C compiler, Python 3.10 or newer, Git history containing the pinned compact revision and the locked Node dependencies in `../reverse-engineered/tests`. The original firmware input may be the vendor ZIP or its HEX. Each build uses a temporary directory and synthetic credentials.

```sh
npm ci --ignore-scripts --prefix ../reverse-engineered/tests
make wifi-bench ARDUINO_AVR_BIN="$ARDUINO_AVR_BIN" ZYMATIC_TEST_HEX="$ZYMATIC_TEST_HEX"
```

## What fits

| Synthetic application | Free in the final code region | Static extension SRAM | Existing features exercised |
| --- | ---: | ---: | --- |
| Compact | 34 bytes before `0x1e000` | 685 / 768 bytes | Ethernet API, image integrity and original commissioning behavior |
| Extended | 386 bytes before `0x1fc00` | 762 / 768 bytes | Ethernet API, OLED, About pairing, remote dial, brew status, history, image integrity and original commissioning behavior |

Both builds link the CC3000 socket state machine, radio arbiter, HTTP bridge, SPI and display arbitration. The radio and Ethernet use a single HTTP request workspace. The linked radio transport exposes stock ownership handoff and an explicit lease on the OEM receive buffer. Those entry points require integration with the original firmware's connection lifecycle and qualification on a board. The existing AVR application regressions exercise incoming Ethernet requests.

The compact profile uses the extended profile's lossless OEM table packers and reuses verified original AVR library routines. It retains its 8 KiB boot boundary. The extended profile reads the UID and device key directly from flash, saving 65 bytes of SRAM. Firmware selection still depends on the installed boot layout.

## Flash layout evidence

The network parser and URL encoder, OLED transactions and keyboard drawing routines occupy less space after their repeated code is shared. The ROM composers preserve their original entry points. Their differential tests check the rewritten routines separately. Together these changes free 4,142 bytes in eight address ranges.

The extended build stores the existing compressed JavaScript, HTML and favicon in space after identical function returns are merged. The linked AVR reader reproduces all 2,725 asset bytes. The tool checks direct branch destinations, skip instructions, original data words that could be code pointers and known patch sites before choosing a return sequence. Each selected return saves and restores the same registers and status flags in the simulator, with two or three extra cycles. Indirect entries and in-brew timing on the physical board still need qualification.

The integrated test checks 54,104 original and packed table accesses on each profile. The simulated active-brew history check remains below the existing 64,000-cycle limit. Use the following commands to inspect one part of the bench.

```sh
python3 wifi/layout/tail_budget.py --toolchain "$ARDUINO_AVR_BIN" --oem-file "$ZYMATIC_TEST_HEX" --simulate
python3 wifi/layout/scatter.py --toolchain "$ARDUINO_AVR_BIN" --oem-file "$ZYMATIC_TEST_HEX" --with-listener
```

## Rear USB and network recovery

PicoBrew documents selecting a Zymatic application HEX in PicoFlash and transferring it through the rear serial USB port. The pinned 1.1.14 download ends at byte address `0x1bbca`. The earliest of the controller's four possible boot regions starts at `0x1e000`. The vendor package supplies application bytes only. PicoBrew's documented procedure gives no method for replacing the serial bootloader or changing boot fuses. [PicoBrew's firmware update guide](https://picobrewcontent.blob.core.windows.net/content/zymatic/ZYMATIC_TROUBLESHOOTING.pdf#page=13) · [Original image and updater evidence](../../../oem/README.md)

The ATmega1284P can update its boot section when the executing bootloader implements the operation and its lock bits permit the write. The BOOTSZ and BOOTRST fuses determine the region and reset entry. The MCU also supports reading fuse and lock state from software. The upstream Sanguino comparison bootloader ignores STK500 universal commands and its own installation guide uses an ISP programmer. A real brewer's bootloader and fuse settings still need identification. These memory checks exercise application code. [Microchip data sheet, chapter 27](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-42719-ATmega1284P_Datasheet.pdf#page=359) · [Sanguino serial command handling](https://github.com/Lauszus/Sanguino/blob/5c8130f99aeecb40945ffc2a68e7e8f96d194de8/bootloaders/optiboot/optiboot.c#L521-L525) · [Sanguino ATmega1284 bootloader installation](https://github.com/Lauszus/Sanguino/blob/5c8130f99aeecb40945ffc2a68e7e8f96d194de8/bootloaders/optiboot/ATmega1284.md)
