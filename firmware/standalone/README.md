# Standalone firmware candidate

`pico-s-pro-standalone.bin` is the raw linked application image for the Pico
S/Pro reconstruction, starting at `0x20004000`. It includes the REST API and
compressed browser assets. `firmware-report.json` records its exact hash,
source hashes, vectors, native link and RAM checks.

This candidate has **not booted on a physical appliance**. It is not a backup
of a user's production firmware, a complete serial-flash image, or evidence
of a validated installation/recovery procedure. Back up the appliance and
complete the documented hardware acceptance work before relying on it.

Rebuild with `make standalone`. The matching debug ELF, linker map and legacy
update-response envelope are generated in `build/standalone/firmware/`; the
CI workflow also uploads the raw image, ELF, map and report. The standalone
firmware does not implement an OTA receiver or an HTTP flashing endpoint.

The API key is a separate per-device serial-flash file. No credential is
included here. See [the standalone guide](../../docs/standalone-api.md) and
[key provisioning](../../docs/standalone-provisioning.md).
