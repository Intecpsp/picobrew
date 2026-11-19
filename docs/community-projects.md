# Community sources and project scope

All project work lives in
[`jeffreydwalter/picobrew-pico-pro-firmware`](https://github.com/jeffreydwalter/picobrew-pico-pro-firmware).
Firmware is organized by machine under `firmware/`, the self-contained Hub
application is under `server/`, and general repository tools are under
`tools/`. Other repositories are read-only sources for reverse engineering,
manuals, recipes and artwork.

| Repository | Role here |
| --- | --- |
| [picobrew-pico-pro-firmware](https://github.com/jeffreydwalter/picobrew-pico-pro-firmware) | Canonical home for machine firmware, the Hub server, catalogue and documentation. Each machine has `oem/`, `reverse-engineered/` and `modified/` directories. |
| [chiefwigms/picobrew_pico](https://github.com/chiefwigms/picobrew_pico) | Source of the complete pinned `app/firmware` archive: all 27 files across Pico S/Pro, Pico C, PicoFerm, PicoStill and Z-series. |
| [jeffreydwalter/picobrew_pico](https://github.com/jeffreydwalter/picobrew_pico) | Read-only fork used for protocol/behavioral references, recipes and artwork. Applicable MIT notices remain with reused material. |
| [picobrew-server](https://github.com/jeffreydwalter/picobrew-server) | Historical source of earlier Hub and catalogue work. Its Tom Herold Python Zymatic application is an external reference, not an installed component here. |
| [picobrew-support](https://github.com/jeffreydwalter/picobrew-support) | Support/manual mirror with source text and images, plus optional local PDF references. Repository-wide content-license scope is not established here. |
| [picopak-library](https://github.com/jeffreydwalter/picopak-library) | PicoPak/ZPak records and synchronization tooling. Imported programs retain source records without invented ingredients or batch sizes. |
| [picobrew-recipes](https://github.com/jeffreydwalter/picobrew-recipes) | Public recipe/site archive supplying 1,320 ingredient recipes, source artwork, original records and conversion warnings. |

## Complete upstream firmware archive

The copy is pinned to Chiefwigms commit
`36acae3003845b72568ef5f7d6cf168acb6f9b69`, covering the complete `app/firmware`
tree `8ec8a87c582aa818d19ff67ddaf9999f9b0850d6`.

| Upstream directory | Destination | Files |
| --- | --- | ---: |
| `pico/` | `firmware/pico-s-pro/oem/upstream/` | 1 |
| `pico_c/` | `firmware/pico-c/oem/upstream/` | 2 |
| `picoferm/` | `firmware/picoferm/oem/upstream/` | 16 |
| `picostill/` | `firmware/picostill/oem/upstream/` | 6 |
| `zseries/` | `firmware/z-series/oem/upstream/` | 2 |

All **27 files / 13,583,860 bytes** retain their original bytes, filenames,
nesting and Git modes. The [upstream manifest](../firmware/upstream-manifest.json)
records every source-to-destination mapping and hash, including the alternate
Pico C 0.1.34 image. Pico S/Pro and Pico C files are OEM HTTP update envelopes;
they are preserved in that form. Their `.bin` names do not identify raw flash
dumps.

The `z1/` and `z2/` directories identify separate development targets. The
upstream images remain grouped under `z-series/` until compatibility with an
individual model is established. The pinned archive contains no Zymatic image.

## Server and firmware direction

The Hub runs from [`server/`](../server/README.md) using its bundled catalogue
and required assets. Its 5,282 catalogue entries and artwork are served
locally. No reference repository is a Git submodule or a required second
checkout for running it. See the [catalogue guide](../server/docs/community-catalog.md).

The [project roadmap](project-roadmap.md) describes the common standalone
firmware API and browser experience. S/Pro is implemented; the other machine
ports remain future work. The exact pre-v1.x S/Pro reconstruction is preserved
in [`reverse-engineered/`](../firmware/pico-s-pro/reverse-engineered/README.md),
with its own [source manifest](../firmware/pico-s-pro/source-manifest.json).
The v1.x work continues in [`modified/`](../firmware/pico-s-pro/modified/README.md).

See [consolidation provenance](consolidation.md), [Hub support coverage](../server/docs/coverage.md)
and [license boundaries](../LICENSES.md).
