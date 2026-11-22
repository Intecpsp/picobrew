# PicoBrew Zymatic

The requested upstream `app/firmware` directory contains no Zymatic image.
This OEM directory remains a placeholder for a separately acquired image.

`reverse-engineered/` is reserved for OEM reconstruction. The host-side
[PicoFlash preparation tool](modified/README.md) verifies and caches the
stock Zymatic 1.1.14 HEX for application restoration. A custom Zymatic
firmware image is still unavailable.

The reserved build/API identity is `zymatic` / **PicoBrew Zymatic**, in family
`zymatic`. Future firmware will follow the shared [model identity contract](../../docs/machine-identity.md).
The root build router rejects this model until its hardware implementation exists.
