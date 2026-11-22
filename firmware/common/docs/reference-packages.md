# Reproducible standalone reference packages

`tools/package_reference.py` assembles complete Z-series, PicoStill and PicoFerm
builds into explicit reference installation artifacts. It accepts an actual
ELF and its complete build report, checks their identity and the report's
source/header/SDK hashes, regenerates the application image, and packages a
verified empty filesystem. It never connects to or flashes an appliance.

This operation defines a custom reference layout. It does not recover an OEM
partition table, assign Z-series images to Z1 or Z2, establish an individual
device's flash capacity, or qualify physical operation. A private complete
backup and device-specific layout/wiring checks remain necessary before any
installation is planned. The [USB service workflow](esp-usb.md) performs these
flash identity and backup checks for individual appliances.

## Command

After building a model with its pinned compiler and SDK:

```sh
python firmware/common/tools/package_reference.py \
  --model picostill \
  --elf firmware/picostill/modified/build/esp32/picostill-standalone.elf \
  --build-report firmware/picostill/modified/build/esp32/build-report.json \
  --esp8266-sdk /path/to/esp8266-2.4.1 \
  --esp32-sdk /path/to/arduino-esp32-9a7946e6859130146e56231c27a4aa177d9175a1 \
  --output firmware/picostill/modified/build/reference
```

The accepted model IDs are `z-series`, `picostill` and `picoferm`. PicoFerm uses
its LX106 ELF/build report and does not require `--esp32-sdk`. ESP32 profiles
also need the pinned ESP8266 SDK because its public SPIFFS C sources provide
the offline formatter; the **actual ESP32 configuration and packed on-flash
ABI** are checked separately against ESP32 SDK objects. This distinction is
recorded in each filesystem report. The ESP32 converter imports `pyserial==3.5`
for offline `elf2image`; no serial port is opened.

`--source-root` can identify a separate checkout matching the build report.
Without it, the package tool uses its own repository. A changed source/header
or ELF is rejected rather than certified by an older report. An existing
generated output directory can be refreshed. The tool validates all new files
in a temporary directory, replaces only its named artifacts, and publishes
the new manifest last. Unrelated files remain intact. Interrupted refreshes
are detectable through the manifest's length/hash checks.

## Explicit layouts

The selected ESP32 profile requires at least 4 MiB of flash and uses a single
factory application. The modified standalone applications do not instantiate
the original remote updater. Their application and history capacities fit
these separate regions:

| ESP32 component | Physical offset | Maximum region |
| --- | ---: | ---: |
| Pinned SDK DIO/40 MHz bootloader | `0x1000` | `0x7000` |
| Generated partition table | `0x8000` | `0x1000` |
| Factory application | `0x10000` | `0x1f0000` |
| Prepared SPIFFS | `0x200000` | `0x200000` |

The partition table also declares NVS at `0x9000`/`0x5000` and OTA-selection data
at `0xe000`/`0x2000`. The package supplies **no replacement image for either**.
The factory-only boot layout does not require an OTA application selection.
Checked modified startup returns an NVS error instead of erasing the partition.

PicoFerm uses the pinned Arduino 2.4.1 `eagle.flash.2m.ld` profile:

| ESP8266 component | Physical offset | Maximum region |
| --- | ---: | ---: |
| eboot plus application | `0x0` | `0x100000` |
| Prepared SPIFFS | `0x100000` | `0xfb000` |
| EEPROM/RF/system tail, no generated write artifact | `0x1fb000` | `0x5000` |

The last 4 KiB of the SPIFFS region is a remainder beyond complete 8 KiB logical
blocks, matching the pinned SDK geometry. The formatter and mounted native
filesystem tests explicitly account for it. Original EEPROM contents are read
without sanitizing, writing or committing them; modified calibration and
history occupy the separate filesystem namespace.

`spiffs.bin` is an **empty, formatted clean-install filesystem**. Writing it
would replace existing modified preferences/history in that region. It is not
an update overlay, credential backup or mechanism for preserving an existing
filesystem. No default API key, Wi-Fi password or private record is generated.
The running firmware never formats a failed filesystem mount.

## Image and package verification

ESP32 application images are produced by the pinned SDK's `esptool.py` with
DIO, 40 MHz and 4 MiB reference settings. The separate repository image parser
checks segment bounds, entry point, XOR footer and appended SHA-256. Every
allocated ELF section byte is then matched by address, including RAM sections
split around mapped flash sections. The bootloader is copied byte-for-byte
from the verified dependency archive. The SDK partition generator produces
the table; an independent decoder checks its exact fields, MD5 and padding.

PicoFerm's standard-library encoder follows the pinned `platform.txt` recipe:
eboot `.text`, `0xaa` padding to offset `0x1000`, then application
`.irom0.text`, `.text`, `.data` and `.rodata`. Both components use their real ELF
entry points and the original ESP8266 XOR image footer. The independent OEM
image parser checks both components, and every decoded section is compared
with its input ELF. The first IROM address must be `0x40201010`, agreeing with
the selected linker and actual image data position. Unexpected allocated
sections, overlaps, truncation, changed mappings or an oversized application
are rejected.

The image construction inputs in `dependencies/reference-images.json` were
compared byte-for-byte with their pinned original public dependency archives.
The package contains `application.elf`, `application.bin`, the original build
report, the image-decoding report, `spiffs.bin`, its formatter/remount report,
and a manifest. ESP32 additionally includes the pinned bootloader and generated
partition CSV/binary. The manifest records each artifact hash, byte count,
selected offset and maximum region, source-verification count, preserved
regions and the explicit absence of hardware installation qualification.

`tools/validate_reference_packages.py` runs real model ELFs through the complete
operation twice and requires identical manifests. It then verifies rejection
of image corruption, shifted component offsets, stale build reports, and a
wrong partition geometry carrying a recomputed valid MD5. Validation keeps a
non-package file intact through repeated generation. Model cases and exact
input/output hashes are recorded in `analysis/reference-package-validation.json`.
The associated filesystem evidence retains all 4,224 history records, reads
the final page after remount, and reserves the following session on every
supported selected profile.
