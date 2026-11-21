# PicoBrew Zymatic: OEM firmware

The requested upstream `app/firmware` directory contains no Zymatic image.
This OEM directory remains a placeholder for a separately acquired image.

PicoBrew distributed the 1.1.14 update through its [signed-in installer page](https://picobrew.com/Members/Software/Zymatic/Installation.cshtml). The [1.1.14 protocol analysis](https://github.com/hotzenklotz/picobrew-server/blob/33bff7fd2234d26a911895acfcd54b741955cd1e/docs/PicoBrew-API.md) reports SHA-256 `eb04c9dafdae032e7770b142f868246caf82eed4d80a0989552fdfd5dfee7b7c` for the original Intel HEX file. A device flash readback may contain bootloader bytes and different padding. Compare decoded address ranges when both artifacts are available. Keep private full-device backups outside the repository.
