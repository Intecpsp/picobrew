# Original PicoBrew firmware from the profiled Pico

`mcuimg2.bin` is the 0.1.34 application image extracted from the verified Pico
Pro flash backup. It is 162,792 bytes and has SHA-256:

`e18c240fa1cf35222674bf3b4fa992a306c6e63b0b99be8793604f199085d702`

It matches the original image used by the S/Pro reconstruction. This is the
application image, not the full private flash backup. Device keys, network
settings and full backups are excluded.

The separately preserved `../upstream/` file is an OEM HTTP update envelope;
its original bytes and filename are retained in the upstream import manifest.

`mcuimg1.bin` is PicoBrew's original recovery v0.0.3 from the same brewer.
Its 50,504 bytes match the logical file in the owner's firmware-only export
of November 17, 2026, the original backup and the live filesystem readback.
Its SHA-256 is `26829ef97df46e6d3667afe8352fa86492f3ddf667371f2d19eafa8468862c63`.
Both files contain the original logical executable bytes. The owner identifies
this unit as a Pico S. The backup and first inspection use the historical
Pico Pro label. Compatibility with other units requires separate evidence.
See the [OEM firmware review](../../modified/docs/device-firmware-review.md).
