# PicoBrew Zymatic: custom firmware

The Zymatic currently runs its stock firmware. The Hub supports stock recipe
sync. A standalone Zymatic firmware image and a Hub-controlled USB programmer
are still needed for conversion.

## Restore the stock application with PicoFlash

PicoBrew distributed a Windows tool and a ZIP containing the Zymatic application
through its [signed-in firmware download page](https://picobrew.com/Members/Software/Zymatic/Installation.cshtml).
Use an original ZIP or HEX for version 1.1.14. The Hub helper can verify and
keep the HEX in the Hub's private data directory:

```sh
python3 firmware/zymatic/modified/tools/usb_stock.py --data /path/to/hub/data --oem-file /path/to/download.zip
```

The command prints the path to the verified HEX. Run it again with only
`--data` to verify and use the saved copy. The SHA-256 identifies the exact
original bytes, including line endings. Its source is recorded in the
[OEM archive notes](../oem/README.md).

On the Windows computer running PicoFlash, connect the idle Zymatic via its
rear USB port, supply power, select the verified HEX with the **[…]** button,
then choose **Flash Now**. Confirm version **1.1.14** in **About Zymatic**
after restart. If the Hub runs on another computer, copy its verified HEX to
the PicoFlash computer. PicoBrew documents this procedure in the firmware
update section of its [Zymatic troubleshooting guide](https://picobrewcontent.blob.core.windows.net/content/zymatic/ZYMATIC_TROUBLESHOOTING.pdf).

This procedure restores the application. Full board recovery, including
reading or replacing the bootloader and EEPROM, requires additional hardware
and programmer evidence. The vendor HEX is absent from the repository.
