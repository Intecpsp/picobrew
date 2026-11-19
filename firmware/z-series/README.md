# PicoBrew Z-series archive

The complete upstream images for this family are in [`oem/upstream/`](oem/upstream/).
They retain their source filenames and bytes; see the repository-wide
[import manifest](../upstream-manifest.json).

`reverse-engineered/` is reserved for OEM reconstruction; `modified/` is
reserved for the custom firmware. Both are currently unimplemented.

The reserved family build/API identity is `z` / **PicoBrew Z**, in family
`z-series`. The archived packages have not yet been assigned to individual Z
variants. Future ports will follow the [model identity contract](../../docs/machine-identity.md)
and require their own hardware implementation before a build is enabled.
