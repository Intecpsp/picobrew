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

Run these commands from the repository root:

```sh
make verify-imports
make firmware MACHINE=pico-s-pro
make firmware-test MACHINE=pico-s-pro
make oem MACHINE=pico-s-pro
```

Bootstrap the selected source component before its first ARM build. The modified
firmware uses `make firmware-bootstrap`; the preserved OEM reconstruction uses
`make -C firmware/pico-s-pro/reverse-engineered bootstrap`. Each component's
guide describes its Python dependencies and existing-toolchain options.

The root Makefile delegates to the selected machine's source directory. Only
S/Pro currently has implemented source builds. Its modified package and local
browser assets are in `firmware/pico-s-pro/modified/firmware/`. Start with the
[S/Pro guide](firmware/pico-s-pro/README.md) for source, build and installation
entry points. No build or test target flashes a brewer.

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
