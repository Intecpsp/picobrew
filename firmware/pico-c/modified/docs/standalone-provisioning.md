# Pico C API key provisioning boundary

The standalone C API reads a separate per-device bearer key from the CC3200
serial-flash file `/local/api.key`. No key is embedded in a public firmware image
or Hub upload package, and the API has no remotely callable key-reset route.

A qualified initial C firmware/key/browser installation and recovery procedure
has not been completed. The S/Pro owner's USB procedure does not establish the
C connector mapping, serial-flash contents or recovery behavior. Do not treat
this page or a successful native link as evidence of physical installation.

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

A future qualified deployment procedure must preserve the original image,
identity, device calibration and network processor files, install the selected
C controller image with the matching browser assets, and provision the key at
`/local/api.key` without formatting unrelated storage. Independent hardware
identification and a tested recovery path are required for that procedure.

Once a correctly provisioned image is already running, enter the device key in
its local website or the Hub's connection form. The Hub checks authenticated
status/discovery and pins the appliance UID before allowing control. A wrong or
missing key yields an authentication error; it does not fall back to public
control or a factory key.

The key is loaded after Wi-Fi becomes available. Rotating it requires a
separately verified local provisioning mechanism and restarting the application;
no package upload, browser action or telemetry setting changes it. Network
installation remains unimplemented in this C version.
