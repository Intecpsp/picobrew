# Web recovery 0.1.0 candidate

This is a separate, native CC3200 recovery application with its own embedded
webpage, authenticated chunk upload, complete SHA256 readback verification,
and explicit image2/image3 boot selection. It is built for the same
`0x20004000` application load address as the original recovery program.

- Image: **42,984 bytes**.
- Conservative build limit: **57,344 bytes**, inside the existing recovery allocation.
- SHA256: `55b63c1c15c829ac131ab57a44310604d136fb285935be1e98527061cea20593`.
- Original stack and heap reservations: **32 KiB each**.
- Unallocated SRAM after the image and reservations: **128,952 bytes**.
- Embedded webpage: **6,271 compressed bytes**, included in the image above.

**This candidate is not installed on the owner's Pico and is not supported by
the current guarded USB installer.** Do not substitute it for the standalone
application in an image2 installation command. The original recovery image and
bootloader remain untouched. Physical startup, radio, output states, flash
writes, boot transitions and power-loss behavior have not been tested.

The candidate refuses to allocate or relocate application images. Image3 is
absent on the current device and appears unavailable in the UI. Its own page
does not depend on the selected application's browser files. Normal boot
selection requires an explicit confirmation; trial boot of image2 returns to
recovery on the next unconfirmed reset under the audited loader's normal
trial sequence. No automatic watchdog rollback is claimed.

Read [the complete design, limits and build procedure](../../docs/web-recovery.md)
before considering a separate guarded installation and hardware acceptance
procedure. `recovery-report.json` records exact source hashes, linked image
layout, runtime boundaries and prohibited-symbol checks.
