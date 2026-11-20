# Pico Z family firmware

The complete upstream images for this family are in [`oem/upstream/`](oem/upstream/).
They retain their source filenames and bytes; see the repository-wide
[import manifest](../upstream-manifest.json).

[`reverse-engineered/`](reverse-engineered/README.md) contains the recovered
Z119 Core/Head application, program runtime, peripheral/network backends,
physical interface and diagnostic commands. It links with the pinned ESP32
SDK. Its per-module evidence distinguishes complete original execution,
returning assertions, modeled SDK boundaries and earlier guarded comparisons.

[`modified/`](modified/README.md) implements standalone 1.2.0: authenticated
local API/browser, local recipes and native maintenance, persistent history,
explicit interrupted-brew recovery, verified provisioning and guarded outputs.
Build with `make firmware MODEL=z-series`; a legacy `MODEL=z` selection is only
an alias for this family build.

The build/API identity is **`z-series` / Pico Z**. The archived packages have
not been assigned to physical Z1/Z2 variants. The custom reference image/layout
and successful software checks do not establish initial-flash compatibility,
physical safety or brewing qualification. See the
[model identity contract](../../docs/machine-identity.md).
