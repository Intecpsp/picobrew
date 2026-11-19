# Pico C

The complete upstream images for this family are in [`oem/upstream/`](oem/upstream/).
They retain their source filenames and bytes; see the repository-wide
[import manifest](../upstream-manifest.json).

`reverse-engineered/` is reserved for OEM reconstruction; `modified/` is
reserved for the custom firmware. Both are currently unimplemented.

The reserved build/API identity is `pico-c` / **Pico C**, in family `pico-c`.
Future firmware will follow the shared [model identity contract](../../docs/machine-identity.md).
The root build router rejects this model until its hardware implementation exists.
