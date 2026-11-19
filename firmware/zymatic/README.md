# PicoBrew Zymatic

The requested upstream `app/firmware` directory contains no Zymatic image.
This OEM directory remains a placeholder for a separately acquired image.

`reverse-engineered/` is reserved for OEM reconstruction; `modified/` is
reserved for the custom firmware. Both are currently unimplemented.

The reserved build/API identity is `zymatic` / **PicoBrew Zymatic**, in family
`zymatic`. Future firmware will follow the shared [model identity contract](../../docs/machine-identity.md).
The root build router rejects this model until its hardware implementation exists.
