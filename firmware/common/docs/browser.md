# Common device browser

The common website follows the authenticated standalone v1 API declared in
`api-schema.md`. Its capabilities, command argument descriptions and supported
settings decide which controls appear. Connecting performs reads only.

| Device | Browser behavior |
| --- | --- |
| Generic Z-series | Local recipes, Z Program Maker programs, encoder controls and the actual 128 × 64 grayscale display. |
| PicoStill | Every advertised temperature and pressure sensor, pump and both fan outputs, native start/essential-oils/clean/stop operations, manual pump and fan settings. |
| PicoFerm | Temperature, native raw pressure and battery readings, fermentation start/stop, explicit depressurized calibration, immediate sampling and sampling interval. Pressure has no inferred kPa conversion. |

Devices without recipes or a front panel neither show those controls nor request
their endpoints. Missing readings remain unavailable. All command acknowledgments
must match the command sent; an unknown write outcome is surfaced without retry.
Calibration asks the user to remove PicoFerm from its keg or adapter, remove its
GRAY seal and confirm depressurization before sending `confirm:true`.

On a Z that advertises `z_program_maker`, the recipe editor can create, import,
edit and export PicoBrew Z Program Maker JSON. It accepts a file from the picker
or by drag and drop. Active steps show their destination, temperature, run time
and drain time. Pause steps wait for the operator. The editor retains fractional
Celsius temperatures, second-resolution times and the program's beer and PicoPak
settings. Older v1 recipes open in their original editor.

## Display and history

Z uses `row-gray4-msb-hex`: even x is the high nibble and odd x is the low
nibble of byte `y*64+x/2`. Four pages are read with a pinned revision. A
`display_changed` conflict discards the whole frame and retries at most twice;
malformed or inconsistent pages fail. Legacy mono page/bit order, rotation and
the Pico C logical viewport remain supported.

History uses each sample page's own `fields` and `field_metadata`, preserving the
schema stored with older sessions. The chart separates different units, while
the raw sample view and JSON/CSV downloads retain every field. Native raw values
are explicitly labeled. This browser can display a legacy 11-column session and
new sessions with additional named sensors and actuator channels.

## Packaging and bindings

Run `python3 tools/embed_web.py` after source changes, or use `--check` to verify
the generated output. Gzip headers have no timestamp. The seven source assets are embedded once, with eight routes because `/` and
`/index.html` share bytes. The brewer serves the original [PicoBrew favicon](https://picobrew.com/favicon.ico) locally at `/favicon.ico`.

Include `picobrew/common_web.h`, initialize `PicoHttpServer` with
`pico_common_web_assets` and `pico_common_web_assets_count`, then call
`pico_http_server_set_asset_io(server, &pico_common_web_asset_io, nullptr)`.
Compile `src/common_web_assets.cpp` with the existing common HTTP transport.
The callback reads bounded chunks directly from firmware constants; ESP8266
uses `memcpy_P`, so unaligned flash bytes are never dereferenced as RAM.

## Verification

`python3 -m unittest discover -s tests -p test_web.py -v` checks deterministic
gzip output, complete native callback reads and display/capability protocol
cases. `tests/web_browser.py` runs the real website in Chromium with authenticated
local API fixtures for all three families at 1280- and 390-pixel widths. Set
`CHROMIUM_PATH` to an available Chromium executable and optionally
`WEB_TEST_OUTPUT` to a screenshot/result directory. It exercises calibration
confirmation/cancellation, false pump settings, fan arguments, paginated
grayscale pixels, history schemas and the API explorer. These are browser and
transport checks; they do not exercise a physical device.

`tools/build_web_assets.py` compiles the generated asset translation unit with
the pinned ESP8266 GCC4.8.2 and ESP32 GCC5.2 toolchains and verifies that all seven
ESP8266 arrays are in the flash section. `analysis/web-target-build.json` records
the exact source, headers, compiler and object hashes. Whole application links
and physical installation remain separate evidence.
