# Zymatic recovery and bootloader identification

The original Zymatic updater uses the rear USB port and the Arduino serial programming protocol. The public 1.1.14 HEX supplies application bytes up to 0x1bbca. Initial conversion preserves flash from 0x1e000 onward, the earliest possible ATmega1284P boot section. To select a larger application, the USB installer checks an existing provisioned UID, matching full external and USB captures, boot bytes, lock bits and boot fuses. If the MCU has been reprogrammed externally since inspection, repeat the measurement before using USB. Neither the HEX nor the updater supplies the original board-specific boot image or fuse settings. [Original package and updater evidence](../oem/README.md)

## Inspect a backup

Run the Hub’s read-only `usb-backup` on the idle brewer. It compares two complete reads before saving the private capture and a `boot-evidence-v1-*.json` report under `usb-stock/zymatic/`. It adds no USB transactions to the normal backup. For a capture saved earlier, run the same inspection offline from repository root.

```sh
python3 firmware/zymatic/modified/tools/boot_evidence.py /absolute/private/avr-flash-HASH.bin
```

The hardware has four possible boot windows. The report fingerprints the complete bytes returned for each window. Sizes are 8192, 4096, 2048 and 1024 bytes, ending at 0x20000 exclusive. `matching_reference` requires an exact SHA-256 match across one complete window. The two current references are the 16 MHz and 8 MHz Sanguino 1.0.2 Optiboot builds for this microcontroller. For each reference, decode the Intel HEX at its declared addresses and fill gaps with 0xff before hashing the final 1024 bytes. These are comparison images. An unknown result is expected for other bootloader builds. A matching binary identifies the returned contents of that window. The physical clock, active boot window, entry path and lock bits require independent evidence. [Pinned Sanguino builds](https://github.com/Lauszus/Sanguino/tree/5c8130f99aeecb40945ffc2a68e7e8f96d194de8/bootloaders/optiboot)

The report labels uniform zero, uniform 0xff, repeated-page and mixed reads. Protected flash can return uninformative bytes. An all-0xff region does not prove it is available for programming. The report contains hashes and read patterns. Keep the underlying full flash capture private.

## Read the actual boot configuration over ISP

On a bench with the board's ISP header, target voltage and pinout independently identified, the optional inspector can use an external Atmel-ICE to read the fuse and lock registers directly. An ISP session resets the controller. Keep the brewer idle and isolate its loads during board-level work. Use a current rear-USB backup from the same brewer. The tool compares the entire application region below 0x1e000 with that capture before accepting an ISP report. Units running identical stock applications can have the same application bytes.

```sh
python3 firmware/zymatic/modified/tools/isp_inspect.py \
  --data /absolute/private/hub-data \
  --usb-backup /absolute/private/avr-flash-HASH.bin \
  --programmer-serial YOUR_ATMEL_ICE_SERIAL
```

The tool uses AVRDUDE 8.1 with the named external programmer. It requires two matching independent reads of the processor signature, flash, EEPROM, three fuse bytes and lock register. It only issues read operations. The report decodes the actual boot size and reset destination, and fingerprints the selected boot window. Private flash and EEPROM copies reside beside the report. Successful reads identify the programmed configuration. Electrical output behavior and recovery across a power cut still require a bench trial. [Atmel-ICE documentation](https://www.microchip.com/en-us/development-tool/atatmel-ice) · [AVRDUDE 8.1 manual](https://avrdudes.github.io/avrdude/8.1/avrdude_4.html)

## What network recovery would require

The ATmega1284P permits flash programming only while the self-programming instruction executes in its boot section. BOOTSZ and BOOTRST fuses determine the boot window and reset destination. Boot lock bits control allowed access. The published application, the AVR signature and a working serial flash operation cannot establish those board settings. Simple serial bootloaders can acknowledge universal programming commands with a constant reply. The Hub therefore records no inferred fuse or lock values from this serial protocol. The existing USB restore writes the application and verifies its surroundings. [ATmega1284P data sheet, sections 27.3, 27.5 and 27.9](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-42719-ATmega1284P_Datasheet.pdf#page=359)

A separate protected network bootloader could recover a single application bank by requesting verified pages again after a power failure. This requires a way to enter recovery and initialize the network *without executing the replaceable brew application*. The current W5100 service uses Ethernet configured by that application. The firmware currently retains its pairing key and network configuration in the application as well. A network update cannot rely on those bytes while erasing or rewriting them. The published firmware has no independently verified recovery transport. A second complete application image also exceeds the available internal flash. [Firmware layout](application.md) · [Original network behavior](../reverse-engineered/network/README.md)

Qualifying an ISP-installed recovery requires an independently verified boot-section backup and fuse/lock read, a documented programming connection for this board, powered/reset output-level measurements, and an authenticated recovery image that fits its selected boot window. Qualification must show the bootloader can recover with an unreadable application, retain the correct device identity, and safely retry a transfer after power loss during page erase, write and final commit. It must also preserve a tested return to the original bootloader and stock firmware. The current rear-USB workflow leaves the boot section intact.
