# Owned Head controller and front-panel lifecycle

`HeadApplication` joins the recovered Head state machines to the actual screen
owners. The containing Core passes its current observation snapshot immediately
before the original Head tick stage. The Head owns all fourteen one-slot input
mailboxes, shared encoder/editor state, screen timers, menu payloads, display
buffers and persistent display text context. It exposes the actual
`core_events` mailbox to Core, HttpBackend and local recipe publication.

The mailbox priority remains Core, button, encoder, text, temperature, time,
strength, valve, quantity, Still, Wi-Fi, Runtime, UI and Azure. One event is
consumed per tick before transient/alarm deadlines, recurring screen work and
display transfer. The Core-facing adapter must copy `HeadUiCoreEnvelope`; its
copy constructor and assignment rebind `command.detail` to its own storage.
Serviced full mailboxes retain the old value and report the original drop-new
behavior rather than a hardware failure.

Physical encoder production, Core power GPIO, SPI transport, clock, EEPROM
persistence, alarm hardware and application observations belong to the
containing platform. Head does not create a second physical encoder. The
public display text context is shared with DisplayConsole, and dirty flags
from the console and screen painters are combined before the real display
transfer. Screen reset preserves the current font and spacing and restores
full display bounds. Core screen power is a separate GPIO operation; Head's
explicit display-command API does not replace it.

## Implemented presentation

`head_presentations.cpp` implements runtime selection, notification and Still
screens; runtime menus and instructions; network, Ethernet and Wi-Fi detail
views; coffee/manual controls; settings; About; recipe lists, loaded programs
and error/result screens. The six original Head console callbacks use the
same real painters and menu owners. Display self-tests are required platform
callbacks; the original Slideshow stability test executes first.

The About alias comes from BackendState.alias, not MSP version. MSP version
has its separate row. Wi-Fi details use the actual BSSID and the original
32-byte format buffers. Network configuration retains fixed status/SSID lines.
All assertion callbacks carry the recovered filename, line and expression
into the common returning diagnostic implementation. Required callbacks fail
explicitly when absent; there are no successful default drawing callbacks.

## Verification and boundaries

`test_head_application.py` links the complete Head implementation and concrete
painters and executes five integration scenarios: setup and shared queue
priority, encoder/editor/Core round trips, settings and display fields, remote
recipe ownership and returning assertions, and console/periodic painter
composition. The four Head application/composition translation units compile
with pinned ESP32 GCC5.2; see `head-application-target-build.json`.

The selected application menus have a separate full original-instruction
comparison in `head-menus-validation.json`. Existing widget, screen, dynamic,
remote-session, core-event, UI, network, numeric and input proofs remain
separate and explicitly describe their dependency boundaries. These tests
validate source behavior and ownership; they do not establish physical
installation, complete task scheduling or brewing validation.
