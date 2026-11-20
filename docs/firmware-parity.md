# Standalone firmware parity

The modified S/Pro firmware defines the common operating model: the appliance
owns local operation, exposes an authenticated API and device website, retains
its own history, and can connect to the optional Hub. The implemented Pico C,
generic Z-series, PicoStill and PicoFerm profiles follow that model through
their own recovered control and hardware code.

This comparison concerns implemented source and software evidence. It does not
turn a generic Z image into a verified Z1/Z2 image or establish physical
installation, sensor accuracy, operating safety or a completed brewing trial.
Zymatic still requires an original application image and hardware evidence.

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

The available OEM archive contains no Zymatic firmware. The Z1/Z2 mappings and
complete device flash layouts are also unverified. Those gaps require original
device evidence; another family's image cannot supply it. Each current image
still needs its corresponding physical installation and operating checks.

S/Pro 1.3.0 supports [Hub OTA through separate recovery](../firmware/pico-s-pro/modified/docs/hub-ota.md). Hardware qualification remains pending, and a qualified
OTA installation/recovery path is not claimed for the C, Z or accessory
reference images. These installation limits remain visible in the API and Hub.
