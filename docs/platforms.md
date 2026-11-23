# Platform setup

The maintained server and project tools target Windows, macOS and Linux. The
original OEM and historical reconstruction/recovery files remain preserved
byte-for-byte; portable entry points select host tools around those archives.

## Hub

Install Go 1.26 or newer and GNU Make, and put them on `PATH`. The Hub includes
its compiled website and uses a pure-Go SQLite driver. Building and running it
does not require Node, npm, a C compiler, or a separate SQLite installation.

From the repository root:

```sh
make server-start
```

The same command works inside `server/`. Both entry points build the native
server, run in the foreground, and use the same `server/data` directory. Press
Ctrl-C to stop. Windows uses native console events.

Without Make, run both commands from `server/` to include the native USB worker for your host.

```sh
go run ./tools/usb-prepare
go build -trimpath ./cmd/picobrew-hub
```

Then run `./picobrew-hub --data ./data` on macOS/Linux or
`.\picobrew-hub.exe --data .\data` in Windows PowerShell.

The executable is `server/picobrew-hub.exe` on Windows and
`server/picobrew-hub` on macOS/Linux. It includes the website and catalog, so
there is no separate web-assets directory to deploy. Existing SQLite databases
keep their schema and format.

Only frontend development and tests need Node. For that workflow use
`^22.22.2 || ^24.15.0 || >=26.0.0`; the root and server `.nvmrc` files select
Node 22.22.2 with `nvm install` and `nvm use` on macOS/Linux. After changing the
website, `npm --prefix web run build:bundle` from `server/` regenerates the
committed package. CI checks that package against a fresh build. See the
[server development guide](../server/README.md#development-and-tests).

## Python and repository tools

Use Python 3.12 for the checked tool environment. The Makefiles choose `python`
on Windows and `python3` elsewhere; set `PYTHON` if your interpreter has another
name. JSON and generated text use explicit UTF-8 and stable line endings.

```sh
python tools/verify_firmware_import.py
python -m unittest discover -s tools -p "test_*.py"
```

Use `python3` in these commands if that is your interpreter name. Import
verification checks original file bytes and hashes on every OS. Windows also
requires a Git checkout to verify the recorded executable modes, which its
filesystem cannot represent as POSIX permission bits. Repository attributes
preserve the exact archived bytes even when Git normally converts text to CRLF.

The catalog builder and curation checks remain inside `server/tools/` and do
not require a firmware checkout except for the optional cross-component starter
comparison described in the [server guide](../server/README.md).

## Firmware compilers and simulations

The maintained S/Pro bootstrap selects a checksum-pinned GNU Arm 10.3 archive
for Linux x86-64, Windows x86-64, or macOS. The SDK and its guarded patches are
the same on all hosts. The vendor's pinned macOS compiler is an Intel binary;
Apple Silicon Macs require Rosetta 2 for that compiler. The Hub itself runs
natively and does not require Rosetta.

The macOS package's ARM libraries contain different vendor build paths, which
would otherwise change the firmware bytes. Bootstrap also downloads the pinned
Linux archive once on macOS and installs only its five Cortex-M4 soft-float
runtime libraries. The native macOS compiler and LTO plugin remain in use.
Linux and Windows already have those exact target libraries. Separate dependency
receipts record the host compiler archive, runtime source archive, and each
target library's hash. Rerun `make firmware-bootstrap` to update an existing
macOS compiler installation to this reproducible runtime.

Firmware simulations also need a native C/C++ compiler: GCC/G++ on Linux,
MinGW-w64 GCC/G++ on Windows, or Apple Clang on macOS. Put the compiler binaries
on `PATH`. Python requirements are recorded in the component's pinned
`requirements-*.txt` files.

From the repository root:

```sh
make firmware-bootstrap
make firmware MODEL=pico-pro
make firmware MODEL=pico-s
make firmware-test MODEL=pico-s
make firmware-usb-test
```

The two S/Pro products share their implementation but have separate build and
package directories. `MODEL` controls the startup branding and the model
reported by the API. A family-only build retains the Pico Pro default. Use
`make firmware-models` to list product identifiers and implementation status;
unimplemented families are rejected before compilation. See the
[machine identity contract](machine-identity.md).

The preserved reconstruction and web recovery images use the same installed
compiler and SDK through an external compatibility adapter:

```sh
make oem
make recovery
```

Without Make, use `python tools/build_archive.py oem` or
`python tools/build_archive.py recovery`. Results are written under
`build/archives/`, with a separate `portable-build.json` recording the adapter
and verification. The commands check the archived files before and after the
build, then compare binary bytes, source identities and image layout with the
preserved packages. On Windows, use a local drive checkout; the old vendor
compiler does not support UNC or extended namespace paths.

The source and binary identity checks remain separate from physical machine
validation. No build or simulation target flashes a brewer.

## USB tools

Owners can run **Brewery → Add machine → USB setup and recovery** from a browser. Connect the USB cable to the Hub host. Release binaries bundle workers for macOS on Intel and Apple silicon, Windows x64, and Linux on x64, ARM64, and 32-bit ARM, including Raspberry Pi. The Hub checks access to the adapter and the available operations for the selected machine. [Owner guide](../README.md) · [Model-specific USB requirements](../server/docs/firmware-packages.md)

The following details apply to the standalone S/Pro developer tools. Their USB backup, preflight and installation commands use PyUSB with the pinned
libusb package. On Windows, direct USB access requires **WinUSB already bound
to the intended CH340 adapter (`1a86:7523`)**. The tools inspect that binding and
claim the interface before configuring UART. They do not install or replace
drivers. WinUSB replaces that adapter's ordinary COM-port function; the serial
probe instead works with the normal CH340 driver and an explicit `COM` port.

On macOS, direct USB capture uses the existing libusb capture path and requires
root. On Linux, use the required device permissions or root. The tools preserve
their existing claim, cancellation and Unix driver-restoration rules.

Private API keys, backups and reports use native file protection: POSIX owner
permissions on macOS/Linux and protected owner ACLs on Windows. File identity,
no-overwrite publication and reparse/symlink checks apply before sensitive data
is read or written. Run commands from the checkout to resolve these helpers
automatically. If copying active USB, key-provisioning, inspection, or telemetry
scripts to another directory, also copy **both** of these files beside them:

- `firmware/pico-s-pro/modified/tools/pico_private_files.py`
- Root `tools/private_files.py`

Keep the command's existing UART and other imported modules alongside it too.
The command reports a missing helper explicitly. A hand-made `chmod` command on
Windows is not a substitute for a private ACL; create keys and backup files
through the provided tools.

Native OS tests and USB simulations do not establish physical flashing or
brewing validation. The supported firmware/machine scope remains the same as
the [project roadmap](project-roadmap.md).
