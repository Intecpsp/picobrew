# PicoBrew Zymatic

Standalone firmware 1.0.0 uses PicoBrew’s original 1.1.14 brew controller. Both builds offer offline recipe selection, a configurable Hub address and wired monitoring. Initial USB conversion installs the compact build. When the installer verifies a sufficiently small bootloader on that brewer, the extended build adds dial pairing, remote operation of the displayed menu, cleaning reminders and stored brew measurements. Physical installation and a supervised brew still need qualification.

- [Original 1.1.14 archive, checksum and provenance](oem/README.md)
- [Reconstructed controls and verification against original AVR instructions](reverse-engineered/README.md)
- [Custom firmware, private build and network capabilities](modified/application.md)
- [Install through the rear USB port, back up, or return to stock](modified/README.md)
