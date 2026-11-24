# Pico C USB setup and stock restoration

The standalone C API reads a separate per-device bearer key from the CC3200
serial-flash file `/local/api.key`. No key is embedded in a public firmware image
or Hub upload package, and the API has no remotely callable key-reset route.

The Hub can use the shared CC3200 USB tool to convert a Pico C from stock 0.1.34 using that brewer’s original 1 MiB flash backup. Choose **Pico C** and its display controller, **SSD1325** or **SSD1309**. The USB tool checks the stock application against the known image for that display. A stock S or Pro image, the other C display image, and unsupported layouts all stop setup before a device write.

Conversion and restoration have passed simulated USB tests for both display builds. Connector access, initial boot, and recovery after power loss on a physical Pico C still need qualification.

## USB setup and restore

1. Capture the idle brewer with the shared CC3200 backup tool. It reads all flash twice and checks the saved copy. Keep this file private. It includes the brewer’s identity and settings.
2. Qualify the backup offline. From `firmware/pico-s-pro/modified`, run the following command with the environment used for the [CC3200 USB tools](../../../pico-s-pro/modified/docs/macos-usb-installation.md). Substitute the identified display.

   ```sh
   .venv/bin/python tools/pico_flash_install.py inspect-stock \
       --model pico-c --display ssd1325 --backup /absolute/path/to/stock.bin
   ```

3. USB setup uses `provision-key`, `install`, then `install-recovery`. Each stage requires a fresh USB programming session and the same `--stock-profile --model pico-c --display … --backup …` options. The installer verifies the live UID, bootloader, boot selection, both named firmware slots and available space. The USB path currently requires the reviewed bootloader and 48-block application slot. It verifies all five website files before opening the application slot for writing. The standalone image comes from this component’s `firmware/standalone/pico-c/<display>/pico-c-standalone.bin`.
4. To restore, use `restore-stock` with the same stock profile and a new `--output` report path. The tool verifies the backup, identity, boot selection and protected slots, restores the original application and recovery, then reads both back. A public C application download supplies no device identity or original recovery bytes.

The last setup stage installs the matching display’s Hub recovery in slot 1. The main application in slot 2 checks that recovery at boot before offering Wi-Fi updates. Wi-Fi settings, the device key and unrelated files stay in place when restoring the application. Because writing an existing application file erases its previous contents, interruption can require another USB restoration attempt.

## Create a private local key file

From `firmware/pico-c/modified/`:

```sh
python3 tools/provision_api_key.py --output private/api.key
```

This command only creates a local file. It generates 32 random bytes represented
as 64 hexadecimal characters plus a newline, creates the directory/file with
private platform permissions and refuses to overwrite an existing file. Keep
the file and full-device backups out of source control, public reports and
shared logs. The key-generation tool does not open a serial port or contact a
machine.

Once a correctly provisioned image is already running, enter the device key in
its local website or the Hub's connection form. The Hub checks authenticated
status/discovery and pins the appliance UID before allowing control. A wrong or
missing key yields an authentication error; it does not fall back to public
control or a factory key.

The key is loaded after Wi-Fi becomes available. Rotating it requires a
separately verified local provisioning mechanism and restarting the application;
no package upload, browser action or telemetry setting changes it. Network
installation remains unimplemented in this C version.
