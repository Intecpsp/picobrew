# Repository layout and source provenance

The canonical project is
[`jeffreydwalter/picobrew-pico-pro-firmware`](https://github.com/jeffreydwalter/picobrew-pico-pro-firmware).
This reorganization starts from canonical commit
`97dd48f905e95ce9122ed3af786ea8ef49b5fd29`. Firmware now lives under
`firmware/<machine>/`, the self-contained Hub application lives in `server/`,
and generic repository tooling lives in root `tools/`.

## Firmware separation

| Location | Contents |
| --- | --- |
| `firmware/pico-s-pro/oem/device/` | Verified Pico Pro 0.1.34 application image extracted from the device backup |
| `firmware/<family>/oem/upstream/` | Complete, unchanged files from the corresponding pinned Chiefwigms archive directory |
| `firmware/pico-s-pro/reverse-engineered/` | Exact 396-file buildable OEM reconstruction snapshot from `ea887709e72c493456583114034cde4aad746099` |
| `firmware/pico-s-pro/modified/` | Current v1.x firmware source, on-device website, installation packages, tools, tests and evidence |
| Other machines' `reverse-engineered/` and `modified/` directories | Explicit placeholders for work that has not been implemented |
| `server/` | Hub Go server, React/TypeScript website, catalogue, required assets and its own development/deployment tools |
| Root `tools/` | General repository checks, including firmware import verification |

The model directories are `pico-s-pro`, `pico-c`, `z1`, `z2`, `z-series`,
`zymatic`, `picoferm` and `picostill`. The shared `z-series` archive preserves
upstream grouping without asserting compatibility with Z1 or Z2. The pinned
upstream archive has no Zymatic image.

## OEM evidence and reconstruction

The device application image is
[`firmware/pico-s-pro/oem/device/mcuimg2.bin`](../firmware/pico-s-pro/oem/device/mcuimg2.bin):
162,792 bytes, SHA-256
`e18c240fa1cf35222674bf3b4fa992a306c6e63b0b99be8793604f199085d702`.
It is an application image; the private full-flash backup and its credentials
are excluded.

The complete upstream archive contains **27 files / 13,583,860 bytes** from
`chiefwigms/picobrew_pico` commit
`36acae3003845b72568ef5f7d6cf168acb6f9b69`. File bytes, original filenames,
nesting and Git modes are preserved in each family's `oem/upstream/` directory.
The [upstream manifest](../firmware/upstream-manifest.json) records all mappings,
SHA-256 values and Git blob identities against source tree
`8ec8a87c582aa818d19ff67ddaf9999f9b0850d6`. Three files are OEM HTTP update
envelopes, retained without decoding or changing the archived originals.

The S/Pro `reverse-engineered/` directory is the exact **396-file** snapshot of
commit `ea887709e72c493456583114034cde4aad746099`, tree
`25a3f356c856e558f40220bd3ed63a8895a0e5b2`. Its
[source manifest](../firmware/pico-s-pro/source-manifest.json) records every
file. This is the buildable reconstruction before standalone v1.x features,
including its documented buffer, I/O and allocator repairs. It is not original
vendor source or a byte-identical OEM rebuild. New development belongs in
`modified/`; the snapshot retains its original documentation and tooling.

The `legacy/` directories within these S/Pro source trees contain an earlier C
decompiler export. They are evidence within the reconstruction, not a Python
Zymatic application and not a substitute for the complete pre-v1.x baseline.

## Earlier source history

| Earlier source | Role in the current project |
| --- | --- |
| Firmware commit `63c49d01784fb502c56fa85b709472e02f923d78` | Existing standalone firmware ancestry before the catalogue and 1.1.0 updates |
| `picobrew-server` commit `d097e68c1be023189fbf240016c2eb57740ff798`, `firmware/` tree `2b2abb70f42f44a98d11218e5527f5bd0245d43a` | Historical source of firmware catalogue/1.1.0 work recovered into the canonical repository |
| The same `d097e68` commit, `hub/` tree `1ff49c2966c0bed9455d2f76bd4e6fcacb647990` | Historical source of the Hub application and offline catalogue, now maintained in `server/` |
| Canonical commit `97dd48f905e95ce9122ed3af786ea8ef49b5fd29` | Parent of this machine-oriented reorganization; the prior root firmware and `hub/` paths are relocated here |

Historical hashes identify source snapshots before subsequent integration
changes. They do not claim that a foreign repository's commit is an ancestor
or that current modified files remain identical to those earlier snapshots.
[import-provenance.json](import-provenance.json) records the relationships.

Tom Herold's Python Zymatic server remains an external research reference. Its
application, dependencies and workflows are not part of this checkout.
Reference repositories are read-only; this reorganization does not modify or
remove their branches, pull requests, files or settings.

## Verification boundary

The import manifests support byte and Git-tree verification of the complete
upstream archive and the exact OEM reconstruction snapshot. The modified
firmware and server require their own build and test checks in the new layout.
Historical CI results do not establish that a new revision passes. Physical
installation and brewing validation remain separate from import verification,
host tests and firmware builds. See the [roadmap](project-roadmap.md) for
implemented support and remaining ports.
