# Pico C

The complete upstream images for this family are in [`oem/upstream/`](oem/upstream/).
They retain their source filenames and bytes; see the repository-wide
[import manifest](../upstream-manifest.json).

[`reverse-engineered/`](reverse-engineered/) contains the typed OEM application
reconstruction and complete native CC3200 builds for the SSD1325 and SSD1309
display variants. Its documentation separates original-instruction verification,
bounded-memory repairs, and hardware qualification. No physical installation or
brewing validation is claimed.

`modified/` is reserved for the standalone custom firmware. Its implementation
and model-router enablement are tracked separately from the OEM reconstruction.

The reserved build/API identity is `pico-c` / **Pico C**, in family `pico-c`.
Future firmware will follow the shared [model identity contract](../../docs/machine-identity.md).
The root build router rejects this model until its hardware implementation exists.
