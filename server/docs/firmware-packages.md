# Firmware packages in PicoBrew Hub

Open a machine's **Firmware** view, choose a version, and click **Install**. The Hub downloads the chosen archive on demand, verifies it, and reuses it on subsequent requests. **Download** saves that version to your computer. Only compatible, idle machines and packages that implement trial confirmation offer **Install**. Historical family builds and reference packages are available for download.

## GitHub source and version checks

**Workspace settings → Firmware** contains the GitHub repository and optional branch, tag, or commit. The default is `jeffreydwalter/picobrew-pico-pro-firmware` on its default branch. A compatible source publishes `firmware/releases.json`. The Hub pins that catalog and its archives to the same Git commit. Each entry also records the build's source revision. A manual **Check for Updates** in the machine view refreshes the metadata immediately.

While physical devices are registered, the Hub checks daily. The admin Overview displays a notice when a machine reports an older numbered version. Failed checks preserve the previous catalog and retry after an hour. A successful check downloads only metadata. Device keys and registered machine identifiers stay on the Hub.

The catalog includes separately targeted S, Pro and C display builds, historical S/Pro family images, and reference packages for the generic Z, PicoFerm and PicoStill. Each catalog entry identifies the release type, model, version, and SHA-256 of the ZIP and application. Named historical versions and pre-release snapshots retain their original bytes. For a new release, add its verified ZIP under `firmware/<family>/modified/releases/<model>/[display/]<version>.zip` and append an entry to `firmware/releases.json`. CI reads every archive and checks that current model-specific applications are represented.

The earlier package upload and single-revision acquisition APIs remain available for external tools. The current source layout is:

- `firmware/pico-s-pro/modified/firmware/standalone/<model>/firmware-report.json`
- The adjacent `<model>-standalone.bin` image.
- `firmware/pico-s-pro/modified/firmware/web/manifest.json` and its five gzip
  payloads.

The standalone package API accepts Pico S, Pico Pro, and both supported Pico C display variants. Reference and historical builds use the version catalog.

## Upload a package

The package upload API accepts a ZIP containing exactly these
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

## Installation

An idle, connected Pico S or Pico Pro with provisioned Hub recovery and standalone firmware advertising `hub_ota_v1` can install a matching protocol-1 package. Open the machine's **Firmware** view and choose a version and click **Install**. Installation is available on a normally started Hub. The Hub verifies the brewer's identity before transfer and confirms the installed image and version after restart. The firmware view reports interrupted or unconfirmed updates and offers an explicit retry for the same package.

The first installation on older firmware requires USB. The bootstrap tool supports one pinned appliance layout and an explicit Pico S application target. See the [setup, recovery and physical validation limits](../../firmware/pico-s-pro/modified/docs/hub-ota.md).

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
| GET / POST | `/api/v1/firmware/updates` | Read cached versions and notices or check the configured source now |
| POST | `/api/v1/firmware/releases/{packageID}/stage` | Fetch and verify the selected catalog archive for download or install |
| GET | `/api/v1/firmware/github` | Inspect model packages at one resolved source commit |
| POST | `/api/v1/firmware/github/import` | Fetch `{repository, commit, model}` into the Hub |
| GET | `/api/v1/firmware/packages` | List saved, validated packages |
| POST | `/api/v1/firmware/packages` | Upload a ZIP as multipart field `file` |
| GET / DELETE | `/api/v1/firmware/packages/{packageID}` | Read metadata or remove a package |
| GET | `/api/v1/firmware/packages/{packageID}/download` | Download the cached ZIP after checking its hash |
| GET | `/api/v1/devices/{id}/firmware` | Read model-scoped packages and installation availability |
| POST | `/api/v1/devices/{id}/firmware/install` | Confirm the package and brewer with `package_id`, `device_uid` and `confirm: true`. HTTP 202 starts a tracked update. |

New imports return HTTP 201; an identical saved archive returns HTTP 200.
Changing the repository after browsing invalidates an import request for the
old source. GitHub errors and rate limits leave saved packages unchanged.
