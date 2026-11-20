# PicoFerm

The complete upstream images for this family are in [`oem/upstream/`](oem/upstream/).
They retain their source filenames and bytes; see the repository-wide
[import manifest](../upstream-manifest.json).

[`reverse-engineered/`](reverse-engineered/) contains verified container and memory
analysis for all 16 preserved releases. These are ESP8266 Arduino images, each
containing a first-sector eboot image followed by an application at offset 0x1000.
Both component checksums are verified. The latest 0.2.7 image embeds Arduino
ESP8266 2.4.1 source paths.

The recovered application has a complete pinned SDK link and original execution
comparisons. [`modified/`](modified/README.md) implements standalone 1.2.0 with
bounded sensor acquisition, verified calibration/configuration, explicit local
recording, persistent history, physical controls and the authenticated common
API/browser/Hub connection. Its continuous API profile does not inherit the
OEM cloud application or its automatic sleep behavior.

Build with `make firmware MODEL=picoferm`. The reference package uses the
explicit 2 MiB linker layout and a prepared, verified filesystem. Software
comparisons and successful links do not establish installation, calibration
accuracy, battery runtime or hardware qualification.
