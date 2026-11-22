# Firmware packages in PicoBrew Hub

Open a machine's **Firmware** view to see wireless update versions for its exact model. Choose a version and click **Install**. The Hub fetches the selected archive, validates it and saves it for reuse. **Download** saves that version to your computer. You can downgrade to an earlier compatible OTA release. The running version cannot be reinstalled through the normal Install action.

## GitHub source and version checks

**Settings → Firmware** contains the GitHub repository and optional branch, tag, or commit. The default is `jeffreydwalter/picobrew-pico-pro-firmware` on its default branch. A compatible source publishes `firmware/releases.json`. The Hub pins that catalog and its archives to the same Git commit. Each entry also records the build's source revision. A manual **Check for Updates** in the machine view refreshes the metadata immediately.

While physical devices are registered, the Hub checks daily. The Brewery page displays a notice when a machine reports an older numbered version. Failed checks preserve the previous catalog and retry after an hour. A successful check downloads only metadata. Device keys and registered machine identifiers stay on the Hub.

The repository release index also preserves historical family builds, Pico C display variants and reference packages for the generic Z, PicoFerm and PicoStill. Each entry records the model, version and SHA-256 of the ZIP and application. The Hub's update catalog and machine version picker include only S and Pro standalone releases with Hub OTA protocol 1. The same rule applies to packages previously uploaded or cached on the Hub. The administrative package API can still list and download saved archives. On Pico C, owners can record their actual display controller independently of wireless update availability.

To publish a new release, add its verified ZIP under `firmware/<family>/modified/releases/<model>/[display/]<version>.zip` and append an entry to `firmware/releases.json`. CI reads every archive and checks that current model-specific applications are represented.

The earlier package upload and single-revision acquisition APIs remain available for external tools. The current source layout is:

- `firmware/pico-s-pro/modified/firmware/standalone/<model>/firmware-report.json`
- The adjacent `<model>-standalone.bin` image.
- `firmware/pico-s-pro/modified/firmware/web/manifest.json` and its five gzip
  payloads.

The standalone package API accepts Pico S, Pico Pro, and both supported Pico C display variants. Reference and historical builds remain accessible in the repository.

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

### Initial USB setup

Stock firmware and older custom installations without provisioned Hub recovery require a computer connected directly by USB. The current writer verifies one physical Pico S in the S/Pro family against its original backup, identity and layout. The original backup is historically labeled Pico Pro. A matching model name alone cannot qualify a different brewer.

Run the local Hub executable from the full checkout on the Mac connected to the brewer, with the USB Python dependencies installed. From `server/`:

```sh
./picobrew-hub usb-install --model pico-s --key-file /absolute/private/path/api.key --data ./data --checkout ..
```

Use the same data directory as the Hub. The command uses its saved original backup, accepts one supplied with `--backup /absolute/path/to/original.bin`, or prompts to capture the stock flash. A fresh capture qualifies only if every byte matches the reviewed original backup. Changes to settings or files on the same brewer can change that hash.

The installer validates the files locally, provisions the private key, installs the application and matching website, and installs the separate Hub recovery. Each write requires its own USB programming session. After verification, restart the brewer and add it to the Hub using that same key. See the [USB setup instructions](../../firmware/pico-s-pro/modified/docs/macos-usb-installation.md) and [OTA qualification notes](../../firmware/pico-s-pro/modified/docs/hub-ota.md).

### Return to stock

On the reviewed physical brewer, the machine firmware view provides a separate stock restore guide with confirmations. The browser explains the consequences and gives the local USB command. Run it from `server/` on the computer connected to the idle brewer:

```sh
./picobrew-hub usb-restore --data ./data --checkout ..
```

The Hub uses its private saved original backup first. If the backup is missing or fails its integrity check, the Hub preserves any existing backup and looks for the two verified OEM images in its cache. For each missing or damaged image, it fetches the pinned Git blob from the canonical firmware repository and verifies its length and SHA-256 before caching it. The USB writer independently verifies the live brewer identity, flash layout, bootloader and boot selection before writing. It restores the original application and recovery, reads both back and confirms that the unused third slot is absent. The final restart is manual.

Stock software removes the standalone website, custom Hub controls and Hub wireless update capability. Wi-Fi settings and the stored device key are preserved. To reinstall custom firmware later, keep the brewer’s original private full-flash backup and the matching host API key file. The USB installer needs both, even if you restored stock using the public OEM images. The restore procedure has passed simulated USB tests. Physical restoration and subsequent boot are pending. [USB restore procedure](../../firmware/pico-s-pro/modified/docs/macos-usb-installation.md#restore-all-oem-firmware-slots)

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
| GET | `/api/v1/devices/{id}/firmware` | Read OTA packages for the identified model, installation availability, and stock restore guide eligibility |
| POST | `/api/v1/devices/{id}/firmware/install` | Confirm the package and brewer with `package_id`, `device_uid` and `confirm: true`. HTTP 202 starts a tracked update. |

New imports return HTTP 201; an identical saved archive returns HTTP 200.
Changing the repository after browsing invalidates an import request for the
old source. GitHub errors and rate limits leave saved packages unchanged.
