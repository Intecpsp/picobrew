# Firmware packages in PicoBrew Hub

Open **Workspace settings → Firmware** to upload a firmware package or fetch
one from GitHub. Saved packages remain available after a Hub restart and can
be downloaded or removed from the Hub. A physical machine's **Firmware** view
shows its current version and packages declared for its model.

## GitHub source

The default repository is `jeffreydwalter/picobrew-pico-pro-firmware`. The
repository is editable, so a fork or another repository with the same package
layout can supply builds. Leave the branch/tag field empty to use that
repository's default branch, or enter a branch, tag, or full commit ID.

Save the source, choose **Check GitHub**, and select the model to fetch. The Hub
resolves the selected revision to one immutable commit before reading any
package files. It records the repository and commit with the saved package.
Changing a branch during a download therefore cannot mix a report, image, and
website from different revisions.

This reads the committed package files; it does not require a GitHub Release,
an Actions artifact, a GitHub account, a local checkout, or a compiler on the
Hub host. The source must be a public repository on GitHub.com. Source lookup
and download happen only when requested. Saving the source does not download
or install firmware.

The current source layout is:

- `firmware/pico-s-pro/modified/firmware/standalone/<model>/firmware-report.json`
- The adjacent `<model>-standalone.bin` image.
- `firmware/pico-s-pro/modified/firmware/web/manifest.json` and its five gzip
  payloads.

Only implemented Pico S and Pico Pro standalone packages are accepted. Other
models require their own implemented package format and validation limits;
renaming an image does not create support for a different machine.

## Upload a package

Choose **Upload firmware package** and select a ZIP containing exactly these
eight files, without an outer directory:

| ZIP path | Contents |
| --- | --- |
| `firmware-report.json` | The unmodified build report |
| `<model>-standalone.bin` | The raw model-specific application image |
| `web/manifest.json` | The matching browser manifest |
| `web/index.html.gz` | Machine webpage |
| `web/app.js.gz` | Machine browser application |
| `web/api-explorer.js.gz` | Machine API explorer |
| `web/history.js.gz` | Machine history interface |
| `web/style.css.gz` | Machine browser styles |

The filename of the ZIP is not used to determine its model or version. A raw
binary, legacy update envelope, ELF, or source-code archive is not a complete
package. The firmware's matching website files are required.

From a full source checkout, build and package the selected model with:

```sh
make firmware-package MODEL=pico-s
make firmware-package MODEL=pico-pro
```

To package an existing committed image without running a compiler:

```sh
python3 firmware/pico-s-pro/modified/tools/package_hub_firmware.py --model pico-s
```

The CLI also accepts `--firmware-directory`, `--web-directory`, and `--output`
for a separately built image and its matching assets. The generated ZIP is
placed under the component's ignored `build/` directory by default. Packaging
is deterministic and checks the inputs before publishing the ZIP.

## What validation means

Both upload and GitHub fetch use the same Hub validator. It checks the model,
family and standalone profile, actual binary length and SHA-256, image vectors
and memory bounds, and the exact browser manifest and payload hashes. It also
checks bounded gzip decompression, original website lengths and hashes, and
the fixed machine file destinations and allocation limits. Archives with
missing, duplicate, unexpected, unsafe, or oversized entries are rejected.

The Hub derives package metadata itself and records how it obtained the bytes.
An uploaded archive cannot claim that the Hub fetched it from GitHub. A saved
package ID is its archive SHA-256; downloading it checks that the cached bytes
still match that identity. Identical archives reuse the existing saved record
and its original acquisition provenance.

These checks establish package consistency and detect corruption. Build
reports and their hashes are not publisher signatures, independent physical
hardware identification, or evidence of a successful installation. A model
match compares declared package and machine model information.

## Installation status

This version manages packages; it does not send firmware writes or reboot a
machine into recovery. The current standalone firmware cannot enter a
qualified local recovery system, and the archived recovery candidate does not
expose an authenticated device identity that the Hub can match to a registered
machine. A successful upload or fetch therefore does not enable an Install
action.

Network installation requires the initial USB recovery setup and an update
protocol with verified identity, idle-state checks, complete readback, and
qualified interrupted-transfer and boot behavior. Existing guarded USB
installation procedures remain separate. See the
[recovery candidate](../../firmware/pico-s-pro/modified/recovery/README.md) and
its documented limits before planning that setup.

## Storage and API

The source configuration and package metadata live in the Hub database;
validated ZIPs live in its private firmware cache. Include the entire Hub data
directory in a disaster-recovery backup to preserve them. The normal workspace
ZIP for recipes, equipment, and brewing history does not include firmware
packages or source configuration. Individual packages can be downloaded from
the firmware manager.

All endpoints below use the normal owner session. Mutations also require the
same-origin CSRF header. No endpoint accepts an arbitrary download URL or
executes code from a package.

| Method | API path | Behavior |
| --- | --- | --- |
| GET / PUT | `/api/v1/firmware/settings` | Read or save `repository` and `ref` |
| GET | `/api/v1/firmware/github` | Inspect model packages at one resolved source commit |
| POST | `/api/v1/firmware/github/import` | Fetch `{repository, commit, model}` into the Hub |
| GET | `/api/v1/firmware/packages` | List saved, validated packages |
| POST | `/api/v1/firmware/packages` | Upload a ZIP as multipart field `file` |
| GET / DELETE | `/api/v1/firmware/packages/{packageID}` | Read metadata or remove a package |
| GET | `/api/v1/firmware/packages/{packageID}/download` | Download the cached ZIP after checking its hash |
| GET | `/api/v1/devices/{id}/firmware` | Read model-scoped packages and installation availability |

New imports return HTTP 201; an identical saved archive returns HTTP 200.
Changing the repository after browsing invalidates an import request for the
old source. GitHub errors and rate limits leave saved packages unchanged.
