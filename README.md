# PicoBrew firmware and Hub

All project development lives in this repository. Firmware is isolated by
machine, with separate OEM images, reverse-engineered OEM source and our
modified v1.x source. The self-contained Go/React Hub lives in `server/`.

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/<machine>/oem/` | Original device images and unchanged upstream firmware packages, with provenance |
| `firmware/<machine>/reverse-engineered/` | OEM reconstruction before our custom features |
| `firmware/<machine>/modified/` | Our custom firmware source, device website, packages and version-specific tools/tests |
| `server/` | Self-contained Hub backend, website, catalogue, container configuration and tests |
| `tools/` | General repository tooling shared across machine families |
| `docs/` | Project scope, source provenance and roadmap |

The populated S/Pro source folders are under
[`firmware/pico-s-pro/`](firmware/pico-s-pro/README.md). Its pre-v1 reconstruction
is an exact 396-file snapshot of commit `ea887709`; its modified source is
version 1.1.0. Both retain their own build tools, dependency manifests, tests,
reference fixtures and documentation. The OEM reconstruction's documented
repairs and validation limits remain attached to that source.

The separate [S/Pro web recovery candidate](firmware/pico-s-pro/modified/recovery/README.md)
is preserved under `modified/recovery/`, with its own pinned source, build tools,
tests and original image. It is an experimental image1 application; installation
and physical recovery are not yet supported or validated.

The [firmware inventory](firmware/README.md) describes every machine directory.
The complete **27-file** `app/firmware` directory from Chiefwigms is copied at
commit `36acae3003845b72568ef5f7d6cf168acb6f9b69`, including Pico S/Pro, Pico C,
PicoFerm, PicoStill and Z-series images. Original filenames and bytes are
preserved. Z1 and Z2 have separate source locations; shared upstream Z-series
images remain grouped until compatibility is verified. The upstream directory
contains no Zymatic image, so that OEM location is a placeholder.

## Run the server

The Hub runs natively on Windows, macOS and Linux. Install Go **1.26 or newer**
and GNU Make. The compiled website is included in the Go binary; Node and npm
are not required to build or run the server.
From either the repository root or `server/`, build and start the Hub with:

```sh
make server-start
```

This builds the Go executable with its bundled website and runs the Hub in the foreground at
[http://127.0.0.1:8080](http://127.0.0.1:8080). Both entry points use the same
`server/data` directory. Authenticated mode also listens on the host's network
interfaces so connected machines can report back to it. The owner password and
first-run setup token are still required. Press **Ctrl-C** to stop.

The [server guide](server/README.md) also has direct Go commands if Make is not
installed. See [platform setup](docs/platforms.md) for firmware compiler, USB
and development prerequisites on each operating system.

### Docker

```sh
git clone https://github.com/jeffreydwalter/picobrew-pico-pro-firmware.git
cd picobrew-pico-pro-firmware/server
docker compose up --build -d
docker compose logs hub
```

Open [http://127.0.0.1:8080](http://127.0.0.1:8080) and use the setup token from
the logs to create the owner account. The server builds and runs independently
of the firmware folders. Its Compose project/service identity remains `hub`,
so the directory rename retains the previous default data volume. See the
[server guide](server/README.md) for native development and custom deployments.

## Build and verify firmware

Activate a Python environment and install the dependencies described in the
[platform setup guide](docs/platforms.md). Run these commands from the repository
root to fetch the pinned compiler/SDK and build the active firmware:

```sh
make verify-imports
make firmware-bootstrap
make firmware MODEL=pico-pro
make firmware MODEL=pico-s
make firmware-test MODEL=pico-s
```

Pico Pro and Pico S share source but produce separate, model-labelled images in
`firmware/pico-s-pro/modified/build/standalone/<model>/firmware/`. Each build
reports its selected model to the Hub and uses it in the device display. The
legacy family-only command defaults to Pico Pro; select `MODEL=pico-s` for an S.

Only S/Pro currently has implemented source builds. Its packaged images are in
`firmware/pico-s-pro/modified/firmware/standalone/<model>/` and shared browser
assets are in `firmware/pico-s-pro/modified/firmware/web/`. The [model registry
and identity contract](docs/machine-identity.md) reserve identifiers for the
other families without claiming working ports. Start with the
[S/Pro guide](firmware/pico-s-pro/README.md) for source, build and installation
entry points. No build or test target flashes a brewer.

### Preserved OEM reconstruction and recovery candidate

Use the root adapter to build either preserved component on Windows, macOS or
Linux. Both reuse the compiler and SDK installed by `make firmware-bootstrap`:

```sh
make oem MACHINE=pico-s-pro
make recovery MACHINE=pico-s-pro
```

Without GNU Make, the same operations are available from the repository root:

```sh
python firmware/pico-s-pro/modified/tools/bootstrap_dependencies.py
python tools/build_archive.py oem
python tools/build_archive.py recovery
```

The Python environment needs `firmware/pico-s-pro/modified/requirements-test.txt`.
The adapter also accepts `--arm-prefix`, `--sdk-root` and `--output-dir`; use
`python3` where that is the name of your Python installation. On Windows, build
these archives from an ordinary local drive path; the pinned compiler adapter
rejects UNC and extended namespace paths explicitly.

| Build | Default output |
| --- | --- |
| OEM reconstruction | `build/archives/oem/pico-pro-recovered.bin` |
| Experimental recovery | `build/archives/recovery/pico-web-recovery.bin` |

The adapter executes the archived build scripts without rewriting their bytes.
It gives those scripts consistent path ordering and separators, then checks the
rebuilt binary, layout and source identities against the preserved package.
Each output directory includes the original builder's report and a separate
`portable-build.json` recording the adapter, host and verification results.
All 396 OEM reconstruction files and 45 pinned recovery files are checked before
and after the build. Generated OEM application archives also use the component's
ignored `build/` directory.

Matching the preserved reconstruction does not establish a byte-identical vendor
firmware rebuild. The recovery image remains an experimental candidate without
supported installation or physical recovery validation.

## Direction and current support

The goal is custom standalone firmware for Pico C, S/Pro, Z1, Z2, Zymatic and
other supported machines, using the S/Pro REST API and browser experience as
the common contract. Each brewer owns its control loop and local operation;
the Hub is optional. Current live support is S/Pro. Other source directories
are explicit placeholders, not completed ports.

The Hub includes the previously requested offline recipe catalogue and assets.
No historical Python Zymatic server or unrelated community application is
included. Reference repositories are read-only. See [AGENTS.md](AGENTS.md), the
[roadmap](docs/project-roadmap.md), [source provenance](docs/consolidation.md),
and [license boundaries](LICENSES.md).
