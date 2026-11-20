# PicoBrew Z-series archive

The complete upstream images for this family are in [`oem/upstream/`](oem/upstream/).
They retain their source filenames and bytes; see the repository-wide
[import manifest](../upstream-manifest.json).

[`reverse-engineered/`](reverse-engineered/) contains the in-progress OEM
reconstruction: verified ESP32 image maps, original source-name evidence,
and host-buildable typed enum and sensor calculation modules with binary
traceability and original-instruction checks. A complete linked OEM
application has not yet been reconstructed. `modified/` remains reserved
for the custom firmware; no Z-series firmware build or installation support
is currently enabled.

The reserved family build/API identity is `z` / **PicoBrew Z**, in family
`z-series`. The archived packages have not yet been assigned to individual Z
variants. Future ports will follow the [model identity contract](../../docs/machine-identity.md)
and require their own hardware implementation before a build is enabled.
