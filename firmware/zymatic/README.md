# PicoBrew Zymatic

The Hub supports recipe sync with stock firmware and USB service of the
original ATmega1284P application. The original 1.1.14 update is archived
outside the upstream `app/firmware` tree. Its hash, download source and
address range are documented in the [OEM archive notes](oem/README.md).

Use the [USB service tools](modified/README.md) to back up readable AVR
flash or restore 1.1.14 through the rear USB port. Original firmware
reconstruction and a standalone Zymatic application are still pending.

The reserved build/API identity is `zymatic` / **PicoBrew Zymatic**, in family
`zymatic`. The [model identity contract](../../docs/machine-identity.md) and
root build router reserve this identity for a future board implementation.
