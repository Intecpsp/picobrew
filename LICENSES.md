# License boundaries

This repository contains code and data with different provenance. Combining
the firmware and Hub does not apply one new license to all components.

| Scope | License or notice |
| --- | --- |
| Firmware source and the preserved `legacy/` C reconstruction | Retained [LGPL-2.1](LICENSES/LGPL-2.1.txt), [TI notice](LICENSES/TI-BSD-notice.txt), per-file notices and [dependency provenance](dependencies/lock.json). These are component notices, not a blanket license for every firmware file. |
| Hub application code | [MIT license](hub/LICENSE), copyright 2026 PicoBrew Hub contributors. Dependencies retain their own licenses. |
| `hub/internal/catalog/data/catalog.zip` and `data/starter-recipes.json` | Pinned community recipes, machine programs, illustrations and original notices. Per-source attribution and license metadata remain in the catalogue manifest and recipe provenance. The Hub's MIT license does not relicense these records or assets. |
| New project-level consolidation documentation and integration files | MIT terms in [the Hub license](hub/LICENSE), without overriding the component boundaries above. |

As recorded in the [firmware README](README.md), the recovery does not assign
a new license to PicoBrew's original binary or embedded assets.

Repository-wide content licenses for the support/manual mirror, PicoPak
catalogue and recipe/site archive were not established during the catalogue
work. Source attribution, original license files and applicable per-file
notices remain with the material. Chiefwigms MIT notices and existing icon
attribution are retained in the bundled catalogue; the historical Python
Zymatic server itself is not included as an application.

See [community sources](docs/community-projects.md) and the
[catalogue guide](hub/docs/community-catalog.md) for source identities and
content boundaries. Public availability alone does not establish a license
grant.
