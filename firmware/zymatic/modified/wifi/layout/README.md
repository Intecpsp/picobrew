# Zymatic Wi-Fi memory checks

These developer checks link AVR code and run synthetic applications under simavr. The released USB installer, firmware packages and both application profiles use their existing builds. The candidate has no device-writing command.

## Compact application

The compact candidate links the CC3000 state machine, shared-radio arbiter and HTTP session bridge alongside the compact Ethernet API and website. Its linker reserves the real radio and arbiter structures. Both transports share the existing HTTP workspace. Radio ownership, interrupts and the SPI bus still require board integration before a Wi-Fi client can reach this image.

The additional code fits by applying the extended profile's lossless packers to the original text, bitmap and SRAM initializer tables. The original 1.1.14 library routine hashes are checked before reuse. The candidate keeps the compact profile's 8 KiB boot boundary and stores its response constants in AVR flash bank 1.

From the Zymatic `modified` directory, run the command with the pinned Arduino AVR GCC 7.3.0-atmel3.6.1-arduino7 toolchain and simavr 1.8. The compact build reads the project's pinned compact source commit. The command prints both flash windows and static SRAM usage after the simulator checks.

```sh
python3 wifi/layout/compact.py --toolchain "$ARDUINO_AVR_BIN" --oem-file "$ZYMATIC_TEST_HEX" --simulate
```

The simulator compares 54,104 original and packed table reads, including resulting registers and status flags. It also boots the linked compact application, serves the existing Ethernet API and checks image corruption and original commissioning behavior.

## Extended application

The extended candidate reads the immutable 32-byte device key and 33-byte UID directly from bank 1. The experiment applies its patch to the committed application in a temporary directory. This saves 65 bytes of static SRAM while preserving the existing HTTP credentials and API. The command compiles that change and runs the existing application regressions.

```sh
python3 wifi/layout/extended.py --toolchain "$ARDUINO_AVR_BIN" --oem-file "$ZYMATIC_TEST_HEX"
```

The credential-only candidate uses 703 of the 768 reserved SRAM bytes. Adding the radio and arbiter structures would use 758 bytes. The extended application already fills its code windows, and the complete listener and radio arbiter still exceed the flash budget.

## Rear USB and network recovery

PicoBrew documents selecting a Zymatic application HEX in PicoFlash and transferring it through the rear serial USB port. The pinned 1.1.14 download ends at byte address `0x1bbca`. The earliest of the controller's four possible boot regions starts at `0x1e000`. The vendor package supplies application bytes only. PicoBrew's documented procedure gives no method for replacing the serial bootloader or changing boot fuses. [PicoBrew's firmware update guide](https://picobrewcontent.blob.core.windows.net/content/zymatic/ZYMATIC_TROUBLESHOOTING.pdf#page=13) · [Original image and updater evidence](../../../oem/README.md)

The ATmega1284P can update its boot section when the executing bootloader implements the operation and its lock bits permit the write. The BOOTSZ and BOOTRST fuses determine the region and reset entry. The MCU also supports reading fuse and lock state from software. The upstream Sanguino comparison bootloader ignores STK500 universal commands and its own installation guide uses an ISP programmer. A real brewer's bootloader and fuse settings still need identification. These memory checks exercise application code. [Microchip data sheet, chapter 27](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-42719-ATmega1284P_Datasheet.pdf#page=359) · [Sanguino serial command handling](https://github.com/Lauszus/Sanguino/blob/5c8130f99aeecb40945ffc2a68e7e8f96d194de8/bootloaders/optiboot/optiboot.c#L521-L525) · [Sanguino ATmega1284 bootloader installation](https://github.com/Lauszus/Sanguino/blob/5c8130f99aeecb40945ffc2a68e7e8f96d194de8/bootloaders/optiboot/ATmega1284.md)
