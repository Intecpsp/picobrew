# Project instructions

## Canonical repository

All implementation, branches, pull requests and publishing belong in
`jeffreydwalter/picobrew`.

`jeffreydwalter/picobrew-server` and other community forks are read-only
references. Do not modify their files, commits, branches, pull requests,
issues, releases or repository settings. Read needed reference material and
implement changes here.

## Required layout and scope

- Isolate firmware by machine under `firmware/<machine>/`.
- `oem/` holds original device images and unmodified upstream packages. Preserve
  original bytes, names, hashes and provenance; distinguish raw images from
  update envelopes and do not infer hardware compatibility from a filename.
- `reverse-engineered/` holds the OEM reconstruction before custom features.
- `modified/` holds our v1.x source, device website and version-specific build,
  test, installation and analysis files.
- Machine-specific tools belong inside the relevant machine/source component.
  General reusable repository tools belong in root `tools/`.
- Root `server/` is self-contained: Hub backend, website, catalogue, assets,
  container configuration and tests. It must build/run without firmware source.
- Keep project-wide documentation, workflows and required license/provenance
  records at the root. Do not import unrelated historical server applications.

The S/Pro OEM reconstruction is pinned at
`ea887709e72c493456583114034cde4aad746099` and copied into
`firmware/pico-s-pro/reverse-engineered/`. Keep every archived file unchanged;
make new firmware changes in `modified/`. Its documented reconstruction
repairs and limits remain valid; it is not original vendor source or a
byte-identical OEM rebuild. Its nested `legacy/` is early decompiler evidence.

The entire upstream `app/firmware` directory is recorded in
`firmware/upstream-manifest.json`. Maintain complete, byte-verified source
coverage when updating that import. Z-series packages stay in `z-series/`
until their mapping to Z1/Z2 is established. Never fabricate a missing OEM
image or claim placeholder source directories contain a working port.

## Product direction and verification

Reverse engineer each machine and implement the common standalone operating
model, versioned API and browser experience established by S/Pro. The optional
server provides shared recipes, fleet control and history. Current live support
is S/Pro; keep completed support distinct from planned ports.

Preserve catalogue attribution and per-file notices. Exclude API keys, private
full-device backups, databases and dependency/build caches from commits. Keep
Compose service/project/data identities stable when only paths are changing.

Run `make verify-imports` after source/image relocation. Validate relevant
component builds and tests when their paths change. Report evidence for the
revision actually checked; build/simulation results do not establish physical
installation or brewing validation.
