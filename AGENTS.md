# Project instructions

## Canonical repository

All project work belongs in
`jeffreydwalter/picobrew-pico-pro-firmware`: firmware, the Hub, catalogue,
documentation, tests, branches and pull requests.

`jeffreydwalter/picobrew-server` and the other community forks are read-only
references. Do not modify their files or create, update or delete their commits,
branches, pull requests, issues, releases or repository settings. Read source
material as needed and implement changes in this repository.

## Product direction

Reverse engineer and implement standalone firmware for Pico C, Pico S/Pro,
Z-series and Zymatic. Each machine should offer the same local operating model,
versioned device API and browser experience established by the custom S/Pro
firmware, with explicit model capabilities and hardware limits. The shared Hub
is optional; each brewer owns its control loop and local operation.

Current live support is S/Pro. Keep implemented support distinct from planned
ports. Existing Python servers can inform reverse engineering; they are not
the architecture for the new machine ports. See `docs/project-roadmap.md`.

## Repository layout and evidence

- Firmware source and tools remain at the repository root. `firmware/` holds
  packaged installation assets; it is not a wrapper around the source tree.
- `hub/` contains the Go server and React/TypeScript application.
- `legacy/` contains the earlier Pico S/Pro C reconstruction. It is not the
  Python Zymatic server; do not import that server as another application.
- Preserve source attribution, per-file notices and pinned provenance when
  reusing community records or assets.
- Keep API keys, private device backups, Hub databases and dependency/build
  caches out of commits.
- Report validation for the exact revision checked. Historical reports and
  workflow definitions do not establish that a new revision passed CI or
  that firmware has been validated on physical hardware.
