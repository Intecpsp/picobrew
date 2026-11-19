# Community sources and project scope

The firmware and Hub live together in
[`jeffreydwalter/picobrew-pico-pro-firmware`](https://github.com/jeffreydwalter/picobrew-pico-pro-firmware).
Other community repositories are read-only sources for reverse engineering,
manuals, recipes and artwork. Imported records retain per-source provenance
and checksums.

| Repository | Role here |
| --- | --- |
| [picobrew-pico-pro-firmware](https://github.com/jeffreydwalter/picobrew-pico-pro-firmware) | Canonical home for all firmware, Hub, catalogue and documentation work. Firmware stays at the root; the Hub is in `hub/`. |
| [picobrew-server](https://github.com/jeffreydwalter/picobrew-server) | Read-only source of the previously published Hub and catalogue work. Its original Tom Herold Python Zymatic server is a research reference, not an imported application. |
| [picobrew_pico](https://github.com/jeffreydwalter/picobrew_pico) | Chiefwigms server fork providing protocol/behavioral references, recipes and artwork. Retain its MIT notices with applicable reused material. |
| [picobrew-support](https://github.com/jeffreydwalter/picobrew-support) | Support/manual mirror with source text and images, plus optional local PDF references. Repository-wide content-license scope is not established here. |
| [picopak-library](https://github.com/jeffreydwalter/picopak-library) | PicoPak/ZPak records and synchronization tooling. Native programs retain exact source records and do not acquire invented ingredients or batch sizes. |
| [picobrew-recipes](https://github.com/jeffreydwalter/picobrew-recipes) | Public recipe/site archive supplying 1,320 ingredient recipes, source artwork, original records and conversion warnings. |

None of the reference repositories is a Git submodule or a required second
checkout for starting the Hub. Its 5,282 bundled catalogue entries and artwork
are served locally. Ordinary dependency installation and the firmware's pinned
toolchain download remain separate build steps. See the
[catalogue guide](../hub/docs/community-catalog.md).

## Firmware direction

The target is standalone firmware for Pico C, Pico S/Pro, Z-series and Zymatic,
with the common local API and browser experience described in the
[project roadmap](project-roadmap.md). S/Pro is the implemented firmware and
live Hub integration. The other machine families currently have planning
profiles; their firmware ports remain future work. Importing archived recipes
does not establish support for running them on a different machine.

The root [`legacy/`](../legacy/README.md) directory is the original Pico S/Pro
C reconstruction. The historical Python Zymatic server remains in its separate
reference repository. No changes to that repository or the other source forks
are part of this project.

See [consolidation provenance](consolidation.md), [Hub support coverage](../hub/docs/coverage.md)
and [license boundaries](../LICENSES.md).
