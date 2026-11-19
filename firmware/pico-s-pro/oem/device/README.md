# Verified Pico Pro application image

`mcuimg2.bin` is the 0.1.34 application image extracted from the verified Pico
Pro flash backup. It is 162,792 bytes and has SHA-256:

`e18c240fa1cf35222674bf3b4fa992a306c6e63b0b99be8793604f199085d702`

It matches the original image used by the S/Pro reconstruction. This is the
application image, not the full private flash backup. Device keys, network
settings and full backups are excluded.

The separately preserved `../upstream/` file is an OEM HTTP update envelope;
its original bytes and filename are retained in the upstream import manifest.
