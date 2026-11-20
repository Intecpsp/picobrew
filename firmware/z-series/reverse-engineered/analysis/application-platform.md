# Concrete Z119 application and historical SDK link

`platform/esp32/application_main.cpp` supplies the real `app_main` entry point.
`FirmwareApplication` owns the recovered Core and Head lifetimes, their shared
mailboxes, the command registry, display diagnostics and the concrete SDK
bindings. The linked application has no simulated successful service adapters.
The implemented diagnostic bodies, their dispatch and the limits of each
comparison are indexed in [application assembly](application-assembly.md).
This is the OEM owner; the [modified profile](../../modified/README.md) has its
own entry point, local services and reference packaging.

## Ownership and startup

The entry point initializes Arduino and creates the original pinned Core task:
16 KiB stack, priority 1, CPU 1. The task initializes the serial console and
Pinger, allocates the application from explicitly internal, byte-accessible
RAM, constructs it and runs the recovered startup ordering. The foreground
loop yields before sampling the clock and ticking Core. A construction or
setup failure is reported and does not enter the normal Core loop.

Head owns the actual Core-event mailbox that Core's backend, program
publication and state transitions share. Commands and Azure/session payloads
are copied into owned storage before posting. An occupied one-slot mailbox
retains the earlier event. No callback stores a pointer to temporary command
data. Live Head observations are refreshed from Core's settings, recipe,
Runtime, MSP, network, safety and backend state.

The About and Wi-Fi detail painters use the recovered Backend alias and
actual BSSID. The Still error indicator uses `polling_stopped`. The displayed
reference pressure comes from the same stored CSV/reference-pressure field.
Display-console and normal screen dirty flags are combined so either path
can cause the framebuffer to flush.

## Concrete peripheral and transport bindings

The owner binds both I²C buses, MSP, MAX11613, LIS3DHTR, BMP280, heater GPIO,
physical buttons/encoder interrupts, display SPI, buzzer LEDC/software timer,
the hardware heater timers and System watchdog. Core's screen-power GPIO and
Head's display commands remain their separate original operations.

HTTP backend, session logging, firmware update, binary download and the live
HTTP diagnostic have separate network-client owners. The ordinary Still transport uses its plain
Wi-Fi client; the authorization transaction selects its separate secure
client. Ethernet/Wi-Fi selection, Azure MQTT, the network probe, incoming HTTP,
EEPROM and Arduino Update use the pinned SDK/dependency implementations.
Original constant backend credentials are generated into the ignored build
directory from byte-verified OEM data; generated values are not committed or
printed in build reports.

Sensor I/O and response formatting run outside the heater critical section.
Only the shared temperature/heater/safety snapshots are copied under the
paired lock. The auxiliary and ADC paths retain original nonfatal diagnostic
continuation instead of treating every nonzero owned status as a reason to
stop later Core services. The buzzer uses a recursive task mutex because its
diagnostic and command paths can nest.

## Concrete diagnostic services

The console uses the real shared assertion counter, clock and 200 ms delay.
The original assertion returns; required caller continuation is retained in
the bound Runtime, MSP, EEPROM, sensor, HTTP and Still implementations. The
older value-only or null-diagnostics APIs retain their explicitly documented
guarded contracts and are not substituted for these application bindings.

`scope` binds ordinary debug tracing. `test_scope` separately binds the original
TestScope protocol, including its beginning watchdog reset and ending
stack-water/assertion checks. The suite marker uses that same counter and
clock, writes its actual result banner, and calls the real DisplayConsole
category/result renderer. That renderer restores the persistent text context
and flushes the resulting frame, including after the returning empty-message
assertion. Nested Basic/backend/software/MSP tests execute their recovered
bodies and invoke actual platform or Head services at their declared boundaries.
See [console dispatch and evidence](application-console.md) and
[display comparisons](display-console.md).

## Link and interrupt evidence

The [full object report](integration-target-build.json) records the recovered
module/adapter inventory, Ethernet2 dependency objects, source/header hashes and
build inputs for the checked revision. The separate
[application link report](application-link.json) records the exact object
report, SDK object/archive, entry-point, linker-script, ELF and map hashes, plus
the section sizes and required symbol addresses from that link.
The SDK archive excludes Arduino's default main and links the owned entry.

The linker preserves the compiler's actual soft-double/integer arithmetic
implementations, placing the ISR dependencies in internal instruction RAM.
It requires the expected application lifecycle, interrupt/data and heater/math
symbols. Both heater callbacks, the encoder callbacks, GPIO reader/writer,
clock routines, critical-section routines and arithmetic dependencies have
internal code addresses; the active application pointer has an internal DRAM
address. Object-level encoder evidence additionally checks the direct SDK
GPIO read and volatile memory barriers.

The linked section sizes do not measure task stacks, application/client heap
allocations or runtime memory pressure. `size` aggregates flash read-only data
under its `data` column; that aggregate must not be described as internal RAM.
Symbol placement does not exercise SDK panic paths, concurrency or cache
behavior on hardware.

```sh
python3 tools/build_esp32_objects.py --toolchain <pinned-toolchain> \
  --arduino <pinned-sdk> --arduinojson <pinned-json> --ethernet2 <pinned-ethernet> \
  --output build/esp32
python3 tools/link_esp32_application.py --toolchain <pinned-toolchain> \
  --arduino <pinned-sdk> --objects build/esp32 \
  --entry platform/esp32/application_main.cpp --output build/application
```

The link command rejects stale source, headers, included implementation files,
objects, generated constants and archives. Its `reconstructed_sources` and
`reconstructed_headers` fields retain repository-relative source identities
for the downstream package verifier; changing recovered source cannot be hidden
by keeping an old ELF or object-report hash. It produces an ELF and map, with
no bootloader, partition table, update envelope or flash-writing operation.
The separate modified build adds its independently checked application image
and explicit custom reference package. Neither path establishes physical Z1/Z2
mapping, an OEM installation layout, powered peripheral timing, brewing
qualification or a qualified OTA installation/recovery path.
