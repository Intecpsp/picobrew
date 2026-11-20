# Pico Z standalone firmware 1.2.0

This application composes the recovered Z119 Core, Head, program runtime,
heater, safety monitors, sensors, physical inputs, display and MSP controller
with the shared authenticated local API, browser, persistent storage and Hub
connection. It produces a complete ESP32 ELF and an independently checked
application image using the pinned historical compiler and Arduino SDK.

The identity is **`z-series` / Pico Z**. The available originals do not establish
which physical Z1/Z2 revisions use this image. The build and package are a
software reference, not a qualified initial installation. No build or test
command flashes or operates a brewer.

## Implemented local operation

The front panel provides local recipes, manual brewing, sous vide, coffee,
maintenance, Wi-Fi and persisted language/temperature/display preferences.
The common browser and Hub expose the same advertised capabilities, the actual
grayscale display and ordered encoder movement/clicks. Local operation and
startup do not require cloud DNS, registration or an Internet connection.

The recipe catalogue retains sixteen recipes with up to fifteen named steps.
Z recipes use direct native valve locations **1–6**; targets are unheated (`0`)
or 60–212 °F, durations are 0–599 minutes, and drains are 0–120 minutes. The
recovered Z builder still applies its valve and local boiling-temperature
limits. An unheated step explicitly stops the preceding PID before valve
movement. Original setup, filter and completion prompts remain interactive.

Archived native Z programs use a different grammar: locations 0–5 map to
valves 1–6, and location 6 means pause. They are retained as reference/export
data in the Hub; they are not silently interpreted as canonical API recipes.
The shared API does not advertise starter programs without an attributed,
model-specific conversion. S/Pro PicoPak data is not installed under a Z label.

Advertised commands are `brew`, `manual`, `sous-vide`, `cold-brew`, `rinse`,
`clean`, `circulate`, `drain`, `rack-beer`, `chill`, `utilities`, `settings`,
`stop`, `clear-stop`, `pause`, `resume`, `encoder`, `resume-interrupted` and
`discard-interrupted`. Recipe and command fields are discoverable through
`/api/v1`. The generic API and display/history formats are documented in
[`../../common/docs/api-schema.md`](../../common/docs/api-schema.md).

## Restart recovery and output inhibition

A local brew reserves its history identity and saves an owned recipe/program
checkpoint before the native start action. Checkpoints update at instruction
changes and at most thirty seconds apart while running. They contain the
remaining native instructions, exact remaining timed wait, display references
and an owned recipe/string table. Editing or deleting a catalogue recipe cannot
change an already retained brew.

On restart, the controller cuts heater outputs and attempts a bounded MSP pump
cutoff. Loading a checkpoint never starts outputs. The physical panel, device
browser and Hub offer an explicit review/resume action. Resumption checks the
filter and controller state; its confirmation asks the operator to verify the
vessel and water. A heated hold reaches its saved target again before its
remaining timer runs. A paused program retains its acknowledgement, and an
unheated/drain stage does not introduce PID heating. A new history session
records the resumed operation; the earlier one remains interrupted.

Requested stop and normal completion write verified tombstones. A controller
fault retains the last valid checkpoint. Corrupt checkpoint banks block new
recorded operations until explicit discard, and failed checkpoint writes cut
outputs. A sudden outage can require repeating up to thirty seconds since the
last successful checkpoint; elapsed time during an outage is not counted as
brewing time.

Stop latches inhibition before returning, disables heater GPIO/PID/countdown,
cancels queued starts and sends a direct pump cutoff. The guard also covers
later native GPIO/MSP writes. Filter removal while required latches the same
stop. A short sensor reply, invalid read byte or failed sensor write cuts
outputs before returning from the acquisition boundary. MSP transactions have
a 50 ms SDK timeout and a sticky I/O fault; an explicit pump cutoff remains
allowed after a fault. Clearing a stop requires the native idle home state
and healthy controller/filter prerequisites and never resumes a session.

## Network, keys and storage

Use the physical UART provisioning interface described in
[`../../common/docs/provisioning.md`](../../common/docs/provisioning.md).
It accepts `wifi <SSID><TAB><password>`, `key <64 lowercase hex>` and `status`.
There is no default API key. Input is not echoed or sent through the OEM serial
command logger. Protected API routes remain unavailable until a valid key is
provisioned; health and static browser files remain public.

The modified network record supports a full 32-byte SSID and 64-hex key, uses
verified storage, and configures station credentials in RAM. Existing OEM
credentials are imported only when the modified network record is definitely
missing; an unreadable modified record is not overwritten by that migration.
The checked startup preserves NVS on initialization failure. Runtime SPIFFS
mounting never formats an unreadable partition.

The modified API transport is Wi-Fi. Original W5500 Ethernet and remote cloud
protocols remain in the OEM reconstruction; the modified settings menu does
not advertise Ethernet/cloud configuration. Local history retains 4,224 samples
across eight sessions, with named sensor/actuator schemas and explicit gaps,
truncation and interrupted sessions. Optional Hub telemetry defaults off and
uses bounded asynchronous networking.

## Build, package and verify

Use Linux x86-64, Python 3.12, Make and a native C/C++ compiler. From the
repository root:

```sh
python -m pip install -r firmware/common/requirements-build.txt
make firmware-bootstrap MODEL=z-series
make firmware-test MODEL=z-series
make firmware MODEL=z-series
make firmware-package MODEL=z-series
make verify-imports
```

`TOOLCHAIN_DIR`, `ARDUINO_DIR`, `ARDUINOJSON_DIR`, `ETHERNET2_DIR` and
`ESP8266_SDK_DIR` can select already acquired pinned references. The last SDK
provides the checked SPIFFS formatter source. `OUTPUT_DIR` and `PACKAGE_DIR`
select generated output directories. The default application output is
`build/reference/`, including `application.bin`, `link/application.elf`, its
map and `standalone-build-report.json`. The package uses `build/package/`.

The explicit custom ESP32 reference layout requires at least 4 MiB flash:
factory application at `0x10000`, length `0x1F0000`; prepared SPIFFS at
`0x200000`, length `0x200000`. The package includes its bootloader, partition
table, empty filesystem and exact image hashes. This is not a recovered OEM
partition table, an OTA updater, or permission to overwrite a device backup.

The native integration suite executes the actual recovered Core/Head/runtime
and shared Engine with explicit hardware/storage boundaries. It covers recipe
ownership and pre-start persistence, real PID stop/resume expansion, physical
input ordering, preferences, coherent grayscale pages, acquisition faults,
latched cutoff, checkpoint corruption/discard and restart recovery. Separate
tests cover the production bounded MSP transport. Target linking checks heater
and encoder interrupt dependencies in internal code and the active owner in
internal RAM. Compiler stack estimates and object sizes do not replace physical
heap/stack high-water measurements or brewing qualification.

The checked [standalone build report](analysis/standalone-build.json) records
the complete linked application's source identities, SDK objects, target memory
layout and generated image checks. The [reference-package validation](../../common/analysis/reference-package-validation.json)
binds the Z, PicoStill and PicoFerm packages to their actual ELFs and build
reports, including deterministic rebuilds, filesystem readback and rejection
of corrupted images, changed addresses and stale build inputs.
