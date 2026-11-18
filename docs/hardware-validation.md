# Physical firmware acceptance and backup status

The source, test executables and complete firmware image have been built and
checked offline. The owner completed a read-only serial-flash backup on the
actual Pico Pro on 2026-11-17: two complete 1 MiB reads matched byte-for-byte.
The [macOS backup record](macos-usb-backup.md) documents that result. No
standalone firmware has been installed or booted on the appliance. A successful
link, emulator comparison or flash read cannot verify peripheral behavior,
application timing, firmware installation/recovery or a heating cycle.

The owner's extracted firmware and boot records were also reviewed. The
main application exactly matches the original v0.1.34 reference. The
[device review](device-firmware-review.md) records the actual bootloader's
selection behavior, the cloud-dependent recovery image, and the flash-space
constraints. The owner also completed live logical-file preflight on
2026-11-17: all three firmware hashes match, image 3 is absent, and
`01 3d 00 20 ba dc cd ab` confirms normal selector 1 for image 2.
API-key creation, readback, existing-file rewrite and final verification
also passed on that date. Application installation/restoration and
standalone boot remain outstanding. The earlier
[third-party screenshot](flash-layout.md) has a different layout and unknown
Pro/C identity.

## Device acceptance record

| Check | Evidence to record | Current result |
| --- | --- | --- |
| Identify device | Main MCU/board revision, auxiliary-controller version, current firmware and serial log | Saved main application is an exact v0.1.34 reference match; remaining hardware identification not run |
| Preserve recovery | Verified existing application/recovery image and boot-file backup; proven way to restore the device | Full 1 MiB backup and live logical-file preflight verified on 2026-11-17; guarded stock restore implemented but not physically validated |
| Validate file writes | API key create, readback, existing-file rewrite and second readback, followed by protected-image checks | Passed on 2026-11-17; original application and protected-file postchecks passed; application-write and interrupted-write behavior remain untested |
| First boot with outputs isolated | Reset reaches setup/main menu; no unexpected pump/heater activation; reset and service-mode behavior | Not run |
| Display/encoder | Native font, rotation, navigation, click debounce and all three language selections | Not run |
| Sensors | Raw ADC counts and independently measured wort/thermoblock temperature at several known points | Not run |
| Outputs | Actual logical pins 5/8/24/27 correspond to heater/drain/shuttle/steam outputs and default off states | Not run |
| Filter interlock | Pin 11 active-low presence; pump pause/resume; missing-filter prompts and alarms | Not run |
| Rotary valve/stepper | Capability query, ready/error replies, every compartment, legacy mapping, torque and home position | Not run |
| RFID/shared SPI | Known 4/7/10-byte cards, card removal/cancel, shared chip selects and simultaneous display activity | Not run |
| WiFi/API | Compatible local server, loss/reconnection, exact requests and UI failure handling | Not run |
| Persistent files | Credentials/preferences/UID, shuttle scaling, restart serialization and failed/short I/O | Not run |
| Update/recovery | Inactive-image write, full binary integrity, boot-file transition, reset and restore after interrupted update | Not run |
| Timers/concurrency | ISR frequency, actual clock rate, SPI/display/network interaction and nonreturning fatal path | Not run |
| Controlled thermal run | Independent temperature reference, cutoff/fault behavior, pump continuity and cooling/drain transitions | Not run |
| Complete program | Recipe, rinse, cancel/restart and maintenance cycles under observed operation | Not run |

Use an isolated output/bench setup before powered thermal operation. The
firmware contains the recovered filter and thermal interlocks, but their
physical execution has not been demonstrated by this project. Do not treat a
software assertion or an emulator's simulated sensor value as a physical
safety test.

## What the offline image checks establish

`tools/build_firmware.py` checks the vector-table load address, Thumb reset
entry, eight-byte initial stack alignment, native entry symbols, physical RAM
extent, 32 KiB heap reservation and binary/ELF correspondence. It rejects test
fixture symbols and undefined functions in the production link.

`tests/test_update_package.py` feeds the complete generated server response to
the actual rebuilt ARM receiver and compares the resulting in-memory file with
the compiled image. This verifies the software encoding and receiver together;
it does not establish serial-flash endurance, atomic writes, real bootloader
behavior or a working recovery route.
