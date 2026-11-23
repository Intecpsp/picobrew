# Standalone firmware parity

The modified S/Pro firmware defines the common operating model: the appliance
owns local operation, exposes an authenticated API and device website, retains
its own history, and can connect to the optional Hub. The implemented Pico C,
generic Z-series, PicoStill and PicoFerm profiles follow that model through
their own recovered control and hardware code.

This comparison concerns implemented source and software evidence. It does not
turn a generic Z image into a verified Z1/Z2 image or establish physical
installation, sensor accuracy, operating safety or a completed brewing trial.
Both the conservative 1.2.0 Zymatic install and the qualified 1.3.x installs retain the original 1.1.14 brew controller.

## Common behavior

| Behavior | S/Pro | Pico C | Generic Z | PicoStill | PicoFerm |
| --- | --- | --- | --- | --- | --- |
| Local operation without a vendor cloud or Hub | Implemented | Implemented | Implemented | Implemented | Implemented |
| Authenticated API v1 and device identity | Implemented | Implemented, including display variant | Implemented, family identity | Implemented | Implemented |
| Device website, settings and API discovery | Implemented | Implemented | Shared browser | Shared browser | Shared browser |
| Optional Hub connection and live readings | Implemented | Implemented | Implemented | Implemented | Implemented |
| Persistent history and exports | Eight sessions sharing 4,224 samples | Eight sessions sharing 4,224 samples | Eight sessions sharing 4,224 samples | Eight sessions sharing 4,224 samples | Eight sessions sharing 4,224 samples |
| Default periodic history interval | 15 seconds | 15 seconds | 15 seconds | 15 seconds | 300 seconds; configurable from 60 to 86,400 seconds |
| Named capabilities for available controls and readings | Brewer profile | Brewer profile | Native Z profile | Accessory profile | Sensor profile |
| Verified source build and explicit model selection | Pico S and Pico Pro images | SSD1325 and SSD1309 images | `z-series` reference image | ESP32 reference image | ESP8266 reference image |

History capacity is a shared pool, not a guarantee that every retained session
contains 4,224 samples. Each session preserves its sampling interval and channel
metadata. Storage exhaustion, interrupted sessions and truncation are reported.
PicoFerm's longer default interval is intentional for fermentation recording.

## Zymatic

| Function | Availability |
| --- | --- |
| Programs | Both versions retain the OEM brew controller, outbound Hub recipe sync and a provisioned offline recipe. |
| Wired device website and Hub | 1.2.0 supports read-only monitoring. 1.3.x adds dial pairing, authenticated input against the current OLED frame and on-device session history. The 1.3.2 local site reads the retained sessions and samples. |
| Firmware service | Rear USB backup, application installation and return to stock 1.1.14. First conversion uses 1.2.0. Subsequent 1.3.x installs require independently measured boot space. |
| Remaining limits | The inbound API uses Ethernet. Firmware updates require USB. Raw actuator control and dedicated network recovery are unavailable. Physical installation, recovery, sensor and brewing trials remain outstanding. |

See the [firmware architecture and verification](../firmware/zymatic/modified/application.md).

## Controls that depend on the machine

| Capability | S/Pro and Pico C | Generic Z | PicoStill | PicoFerm |
| --- | --- | --- | --- | --- |
| Installed recipe catalogue | Sixteen recipes, up to fifteen steps | Sixteen recipes, up to fifteen steps | Not advertised | Not advertised |
| Starter recipes and restore | Attributed model recipes | Requires an attributed native conversion; not advertised | Not applicable | Not applicable |
| Physical panel and remote display/input | Native monochrome panel and encoder | Native grayscale panel, four display pages and ordered encoder input | No panel invented | No panel invented |
| Normal operating programs | Native brewer programs and maintenance | Native manual, beer, sous vide, coffee and maintenance programs | Native manual, essential-oil and cleaning modes | Explicit recording, sampling and calibration |
| Interrupted-operation recovery | Explicit confirmation with unchanged-recipe checks | Explicit confirmation of a retained owned recipe/program checkpoint | Interrupted history; no automatic mode restart | Interrupted history; no automatic recording restart |
| Outputs | Recovered brewer controls | Recovered heater and MSP pump/valve controls | Pump and two fan channels; no heater | No heater, pump or fan controls |
| Readings | Recovered brewer sensors | Recovered brewer and auxiliary sensors | Named temperatures and pressure | Temperature, raw pressure and battery voltage |

Z canonical API recipes use direct native valves **1–6**. Archived native Z
programs use locations **0–5** for valves and **6** for pause. The Hub preserves
those archived programs as reference/export material until an explicit
conversion is supplied; it does not deploy their numbers as canonical recipes.
PicoFerm's pressure reading retains its evidenced raw units; battery readings
use the recovered ADC-to-voltage conversion and are reported in volts.

The modified Z profile uses Wi-Fi for its local API. Its recovered OEM source
also retains the original W5500 Ethernet and vendor protocols. Modified
firmware does not expose those cloud settings as required setup steps.

## Recipe checks and manufacturer care

The Hub checks ingredient recipes against the Pico S, Pro, C, generic Z and Zymatic brewing profiles. A recipe can remain in the planning library when the selected brewer cannot run it. During transfer the Hub applies the target’s limits and rejects unsupported mash methods, mash routes that bypass the grain, and schedules that would change timed hop contact. An intermediate drain can interrupt a timed hop addition. The Hub flags that timing conflict before transfer. It also blocks a generated stock Z export if an unheated step would inherit a previous heating target. Standalone Z can execute that transition. PicoFerm and PicoStill have their own session controls.

Single and bulk transfers both check the connected firmware's reported slot count, maximum steps, and support for zero-minute heating steps. During a bulk replacement the Hub reads each destination slot again before overwriting it. The transfer receipt identifies any remaining recipes that were skipped after a slot changed. The device protocol has separate read and write requests, so a simultaneous edit by a second client can still occur between the two requests.

The local sites and Hub machine pages show the applicable care schedule. On Pico S, Pro and C, rinse after each brew and run Deep Clean after three brews or after descaling. For Deep Clean, put the tablet in the brewing keg on S and Pro, or in the center of the step filter on C. The 20-brew descaling interval applies when using reservoir water other than distilled or reverse osmosis water. On generic Z, rinse after each brew and clean after three brews or before extended storage. On Zymatic, clean the inline filter and rinse after each brew, then deep clean every three to four brews and rinse again. The PicoStill and PicoFerm pages cover condenser care and fermentation seal handling respectively. Opening the guidance does not start hardware. [Pico S manual](https://picobrewcontent.blob.core.windows.net/content/pico/Pico_Manual.pdf#page=127) · [Pico Pro manual](https://picobrewcontent.blob.core.windows.net/content/picopro/PicoPro_Manual.pdf#page=129) · [Pico C manual](https://picobrewcontent.blob.core.windows.net/content/picoc/PicoC_Manual.pdf#page=109) · [Z manual](https://picobrewcontent.blob.core.windows.net/content/z/Z_Manual.pdf#page=36) · [Zymatic manual](https://picodevblob.blob.core.windows.net/content/zymatic/Zymatic_Manual.pdf#page=23) · [PicoStill manual](https://picobrewcontent.blob.core.windows.net/content/picostill/PicoStill_Manual.pdf#page=58) · [PicoFerm manual](https://picobrewcontent.blob.core.windows.net/content/picoferm/PicoFerm_Manual.pdf#page=5)

## Verification and entry points

The component evidence includes native controller integration, comparisons
against original instructions, real filesystem and allocator replays, complete
SDK links, checked application images, and browser/Hub tests. Those checks have
different scopes; a passing diagnostic fixture alone does not establish a full
application or a physical-device result.

- [S/Pro source, model builds and operating contract](../firmware/pico-s-pro/modified/README.md)
- [Pico C source and explicit display variants](../firmware/pico-c/modified/README.md)
- [Z control, output inhibition and restart recovery](../firmware/z-series/modified/README.md)
- [PicoStill native controller and reference package](../firmware/picostill/modified/README.md)
- [PicoFerm bounded acquisition, memory replay and reference package](../firmware/picoferm/modified/README.md)
- [Shared API, display and history schema](../firmware/common/docs/api-schema.md)
- [Hub integration and capability limits](../server/docs/coverage.md)
- [Model identity and installation boundaries](machine-identity.md)
- [Complete Z standalone application and checked image](../firmware/z-series/modified/analysis/standalone-build.json)
- [All three reference packages: actual ELF, filesystem and rejection checks](../firmware/common/analysis/reference-package-validation.json)

Run the root `firmware-test`, `firmware`, and `firmware-package` targets with
the documented model selection. Pico C additionally requires an explicit
display variant. No build, package or test target flashes an appliance.

## Inputs still required for physical qualification

The Zymatic 1.1.14 application is preserved in a separately pinned support archive. The Z1/Z2 mappings and
complete device flash layouts are unverified. Those gaps require original
device evidence; another family's image cannot supply it. Each current image
still needs its corresponding physical installation and operating checks.

S/Pro 1.3.0 supports [Hub OTA through separate recovery](../firmware/pico-s-pro/modified/docs/hub-ota.md). Hardware qualification remains pending, and a qualified
OTA installation/recovery path is not claimed for the C, Z or accessory
reference images. These installation limits remain visible in the API and Hub.
