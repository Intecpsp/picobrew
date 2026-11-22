# Zymatic USB service

The Hub can read the application controller’s flash and restore PicoBrew’s original 1.1.14 application through the Zymatic’s built-in rear USB port. The stock brewer also supports Hub recipe sync. Conversion to standalone firmware requires a custom Zymatic image, which is still unavailable.

## Back up or restore from the Hub computer

Use an idle brewer. Connect its rear USB port to the computer running the command, power it on and close PicoFlash and any serial monitors. Opening the bootloader can reset the controller. Install Python 3, `pyserial==3.5` and AVRDUDE **8.1** on that computer. Put the AVRDUDE executable on `PATH` and keep its packaged configuration installed. List local USB ports with `python3 -m serial.tools.list_ports -v` and select the PicoBrew port labeled `20A0:421E`. [AVRDUDE 8.1 releases](https://github.com/avrdudes/avrdude/releases/tag/v8.1) · [PicoBrew updater evidence](../oem/README.md)

Run these commands from the firmware checkout and substitute the actual data directory and port, for example `COM4` on Windows. Run backup to keep a private copy of the controller’s readable flash. Use restore when you intend to replace its application with stock 1.1.14.

```sh
python3 firmware/zymatic/modified/tools/usb_stock.py --data /path/to/hub/data --action backup --port SERIAL
python3 firmware/zymatic/modified/tools/usb_stock.py --data /path/to/hub/data --action restore --port SERIAL
```

Native restore also requires an identifiable USB serial number or USB bus location. Both commands check the USB identity and AVR signature, then read the complete 128 KiB flash twice. A matching read pair is saved under `usb-stock/zymatic/avr-flash-<sha256>.bin` in private Hub data. A matching known stock application is cached separately for reuse. Read protection can limit what the bootloader reports. The capture covers the AVR flash visible over USB. The separate EEPROM, fuses, lock bits and CC3000 Wi-Fi controller are outside its scope.

Restore prefers the verified saved stock application. You may also supply an exact original ZIP or HEX with `--oem-file FILE`. If neither a stock prefix nor the HEX is available locally, the helper retrieves the [pinned public archive](../oem/README.md) and verifies both the archive and the HEX. A mismatching local copy stops restoration. The original setup archive can also be prepared on another computer and copied over for offline use.

If stock 1.1.14 is already present the command finishes after verifying it. Otherwise it asks for two confirmations on the local terminal. Keep power and USB connected while it rechecks the full baseline on the same connection used for writing, writes the application pages and reads all 128 KiB back through a new connection. AVRDUDE’s `-D` disables a chip erase. The page-aligned write preserves the 54 bytes just after the application and stays below all possible bootloader addresses. The tool compares the full readback to the original application plus the captured surrounding bytes. Disconnect USB afterward, restart the machine and check **About Zymatic** for **1.1.14**. [AVRDUDE command options](https://avrdudes.github.io/avrdude/8.1/avrdude_4.html)

## Use the original Windows updater

Prepare the original 1.1.14 HEX by running the following command. It reuses the verified local HEX or downloads and verifies it from the pinned archive. Add `--oem-file FILE` to import an original ZIP or HEX you already have.

```sh
python3 firmware/zymatic/modified/tools/usb_stock.py --data /path/to/hub/data
```

On the Windows computer running PicoFlash, connect the idle Zymatic over USB, power it on, choose the prepared HEX with **[…]** and select **Flash Now**. If the Hub is on a different computer, copy the verified HEX to the PicoFlash computer first. Follow PicoBrew's power and USB instructions and confirm **1.1.14** in **About Zymatic** after restart. [PicoFlash archive](https://github.com/chiefwigms/picobrew_pico/blob/36acae3003845b72568ef5f7d6cf168acb6f9b69/app/static/support/legacy/common/PicoFlash.zip) · [PicoBrew troubleshooting guide](https://picobrewcontent.blob.core.windows.net/content/zymatic/ZYMATIC_TROUBLESHOOTING.pdf#page=16)

## Validation

Run `python3 -m unittest discover -s firmware/zymatic/modified/tests` for the offline regression suite. On macOS or Linux with AVRDUDE 8.1 installed, prefix the command with `AVRDUDE_SERIAL_TEST=1` to exercise the actual AVRDUDE executable against a simulated USB serial bootloader. This checks the exact application page writes, preservation of surrounding bytes and the refusal to write after a baseline mismatch. The host workflow has been exercised in simulation. A physical Zymatic backup and restore remain to be qualified.
