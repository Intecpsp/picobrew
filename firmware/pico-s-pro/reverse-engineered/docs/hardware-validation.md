# Physical acceptance remains unperformed

The source, test executables and complete firmware image have been built and
checked offline. No Pico Pro is attached to this workspace. Every physical
result below is therefore **not run**, rather than inferred from the software
results. A successful link or emulator comparison cannot verify electrical
wiring, peripheral firmware, flash contents, timing races or a heating cycle.

## Device acceptance record

| Check | Evidence to record | Current result |
| --- | --- | --- |
| Identify device | Main MCU/board revision, auxiliary-controller version, current firmware and serial log | Not run |
| Preserve recovery | Verified existing application/recovery image and boot-file backup; proven way to restore the device | Not run |
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
