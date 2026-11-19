# Consolidation in the firmware repository

The canonical project is
[`jeffreydwalter/picobrew-pico-pro-firmware`](https://github.com/jeffreydwalter/picobrew-pico-pro-firmware).
Firmware source remains at the repository root, and the shared Hub is added
under `hub/`. The earlier combined publication in `picobrew-server` is a source
snapshot for recovering completed work. That reference repository is not a
destination for further changes.

## Source identities

| Component | Source identity | Treatment here |
| --- | --- | --- |
| Firmware history | `63c49d01784fb502c56fa85b709472e02f923d78` on the firmware repository's `feature/standalone-rest-api` branch | Existing ancestry and root layout retained |
| Firmware catalogue and 1.1.0 updates | `firmware/` at `d097e68c1be023189fbf240016c2eb57740ff798` in `jeffreydwalter/picobrew-server`; tree `2b2abb70f42f44a98d11218e5527f5bd0245d43a` | Recovered into the firmware repository root, with integration paths updated |
| Hub and offline catalogue | `hub/` at the same `d097e68c1be023189fbf240016c2eb57740ff798`; tree `1ff49c2966c0bed9455d2f76bd4e6fcacb647990` | Added under `hub/`, with repository paths and project-direction documentation corrected |

These pinned identities describe the source snapshots before integration
changes. They do not assert that every destination file remains byte-identical
or that a source commit from the other repository is part of this repository's
ancestry. Machine-readable identities are in
[import-provenance.json](import-provenance.json).

## What `legacy/` means

The existing root `legacy/` directory contains the earlier **Pico S/Pro C
reconstruction**, including its analysis and checksums. See its
[README](../legacy/README.md). It remains firmware reverse-engineering evidence.

Tom Herold's historical Python Zymatic server originated in
[`hotzenklotz/picobrew-server`](https://github.com/hotzenklotz/picobrew-server)
and was inherited by the `jeffreydwalter/picobrew-server` fork. Its application,
Python dependencies and workflows are not imported into this repository.
Existing attributed artwork in the Hub catalogue retains its source notices.

## Layout and operation

- Firmware source, tools, tests and documentation stay at their original root
  paths. `firmware/` continues to contain packaged installation assets.
- `hub/` contains the independently runnable Go/React application and its
  Compose configuration. A second repository checkout is not needed to run it.
- Firmware and Hub share the bundled recipe catalogue's stable starter IDs.
- Local credentials, device backups, Hub databases and build/dependency caches
  are not part of the imported source.
- Reference forks are read-only. This migration does not change or remove their
  existing branches, pull requests, files or settings.

## Validation scope

The source identities above establish provenance. Integration must be checked
against this repository's paths, workflows and build outputs. Historical CI
results in a different repository do not establish that this revision passes.
Physical installation and brewing validation remain separate from packaging,
host tests and firmware builds. The [roadmap](project-roadmap.md) distinguishes
the implemented S/Pro firmware from the other planned machine ports.
