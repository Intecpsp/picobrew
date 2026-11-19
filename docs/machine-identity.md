# Machine identity and product builds

The firmware declares its product model at compile time. This identifies the
image that was built; it does not inspect hardware or establish compatibility
with a different product. Select the intended product before building.

The canonical identifiers are recorded in [`firmware/models.json`](../firmware/models.json):

| Model ID | Display name | Source family | Custom firmware |
| --- | --- | --- | --- |
| `pico-pro` | Pico Pro | `pico-s-pro` | Implemented |
| `pico-s` | Pico S | `pico-s-pro` | Implemented |
| `pico-c` | Pico C | `pico-c` | Planned |
| `z` | PicoBrew Z | `z-series` | Planned; archived Z packages are not yet mapped to individual variants |
| `zymatic` | PicoBrew Zymatic | `zymatic` | Planned |

The root build router accepts a model through `MODEL` or `MACHINE`:

```sh
make firmware MODEL=pico-s
make firmware MODEL=pico-pro
make firmware MACHINE=pico-s
make firmware-test MODEL=pico-s
make firmware-models
```

`MACHINE=pico-s-pro MODEL=pico-s` is also supported. The original family-only
command, and a command without either selection, default to `pico-pro`.
Conflicting selections and unimplemented product models fail before invoking
the component build. Selecting a different model never relabels an S/Pro image
as a Pico C, Z, or Zymatic image.

Within the S/Pro component, use `make standalone MODEL=pico-s` or
`python tools/build_firmware.py --standalone --model pico-s`. Build outputs are
isolated under `build/standalone/pico-s/firmware/` and
`build/standalone/pico-pro/firmware/`, with matching `<model>-standalone`
filenames. Committed installation packages use the same per-model layout under
`firmware/standalone/`. The device website assets are shared.

## API contract

Model-aware firmware exposes stable identity fields alongside its existing
version and capabilities:

```json
{
  "model": "pico-s",
  "model_name": "Pico S",
  "model_family": "pico-s-pro",
  "firmware_version": "1.2.0"
}
```

The startup display, device website, REST identity, and build report derive
their identity from the same compile-time selection. Future machine ports
should use these field names and their registry identifiers. They still need
their own hardware implementation and capability checks.

The Hub reads the authenticated model during connection and associates it with
the verified device UID. The UID identifies the particular machine; the model
selects its equipment profile. Reported unsupported models cannot enable a
different machine's control adapter. Firmware that predates model reporting
can still use an explicit profile selection; it is not guessed to be a Pro.
Correcting a current machine label does not rewrite archived brew snapshots.

## Hub-managed reporting

Connecting a machine also configures it to report readings to that Hub.
Ordinary setup requires no receiver form on the firmware website. The Hub
verifies the appliance identity and waits until it is idle before changing
the saved reporting connection.

Advanced destination settings remain editable in the Hub for unusual network
layouts. Explicit manual settings survive reconnects and Hub restarts; select
automatic setup to return to the Hub-managed destination. Authenticated polling
and history reads remain authoritative even when forwarded readings are
unavailable. Forwarded readings do not authorize machine commands.
