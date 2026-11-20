# Shared standalone appliance services

The device contract is in `include/picobrew/device_services.hpp` and the wire
schema is in `docs/api-schema.md`. S/Pro and C retain their existing profiles.
The common engine serves Z-series, PicoStill and PicoFerm adapters with explicit
capabilities and model-specific operations.

`include/picobrew/http_server.h` and `src/http_server.c` are unchanged copies
of the existing S/Pro modified-profile portable HTTP transport. They retain its
bounded request parser, 64-character bearer-key handling, nonblocking I/O budget,
streamed static assets and response deadlines. Shared flash assets use the
same `PicoHttpAssetIo` interface. Their provenance is the repository's S/Pro
modified source; no OEM source archive is altered.

`standalone_engine.hpp` defines construction, startup, foreground polling,
HTTP dispatch, bounded events and the persisted recipe catalogue needed by
local front panels. Device/storage/clock callbacks remain platform-owned.
Optional outbound telemetry uses asynchronous connection and bounded I/O;
slow networks must return pending/AGAIN instead of blocking device service.

## Implemented engine

The portable engine implements the full advertised v1 discovery/status,
preferences, commands, recipe catalogue, display paging, events, persistent
history and optional Hub forwarding routes. Capabilities determine available
routes and argument fields. Device callbacks validate native commands and
recipe conversions before execution. Stop bypasses a failed persistent store
and reaches the device's synchronous output-inhibition callback.

The HTTP composition loads the device's separately provisioned 64-character
key into `pico_http_server_set_key()`. The engine checks the transport's
authentication flags again; health remains public. No default key or key in
the telemetry payload is introduced. Caller-owned request bodies may alias
the response arena: all parsers finish before a response overwrites it.

Storage records have an explicit little-endian version, generation, logical
name hash and CRC32, with two independently verified banks. The recipe
catalogue references exact committed recipe generations, so an interrupted
catalogue update leaves the prior recipe readable. Deleted slots retain their
generation before reuse. Settings are validated, applied and verified, with a
device rollback on a failed persistence operation. Missing, corrupt and I/O
failures remain distinct; the engine never formats or silently creates a
replacement over an unreadable store.

History retains **4,224 records in a shared pool across eight sessions**. The
pool uses 176 chunks of 24 records, with only one chunk loaded for an append.
Each session has a stable schema describing every named sensor and actuator;
old rows are never relabelled from the current hardware snapshot. Native
elapsed-time and signed step columns preserve their exact 32-bit values; the
six bounded state/flag columns use one byte each. Sensor values use
IEEE binary32 with a canonical missing-value marker. JSON never emits NaN or
infinity. The first development chunk format remains readable and compacts
only on a verified append. Unsupported actuator level/RPM columns are omitted
using explicit structural capabilities; temporary invalid readings remain
null in the fixed session schema.

Sampling defaults to every 15 seconds plus state changes; sensor devices can
supply a longer actual acquisition interval, captured per session. At Ferm's
300-second default, 4,224 regular samples cover about 14.7 days. Missed periods,
truncation and storage errors are explicit. The catalogue is checkpointed
after each verified sample. A session exceeding the 32-bit elapsed-time range
is explicitly truncated, never wrapped into apparently earlier rows. Full pools evict completed sessions before their
chunks are reused; the active session is never silently overwritten.

History pages pack up to 32 records into the 4096-byte arena, with at least
two fitting the real Z/Still/Ferm schemas. Every sample page includes its
session's fields and named-field metadata; `next_offset` is authoritative.
The history resource advertises the maximum and minimum page sizes.

RAM allocations follow actual capabilities and session fields. Accessories
allocate no recipe catalogue. A history schema owns only its field descriptors,
and complete-bank CRC/generation checks precede direct slice reads into that
allocation. A sample chunk reserves its actual schema width, including room to
read the legacy encoding; reclaiming a wider old chunk first verifies only its
metadata. Telemetry measures the serialized frame before allocating and releases
the buffer on success, failure, disable or rebinding. These changes preserve
the wire format and the full 4,224-record retention limit.

`prepare_session()` commits an identity and sensor schema **before** native
program start. The machine passes the same nonzero token in later recording
snapshots, and the engine adopts the reservation without allocating a second
session. Call `cancel_prepared_session()` if native start fails. A restarted
recording becomes interrupted; it never starts itself. A missing or corrupt
session counter blocks new operations instead of resetting exported IDs.
Physical preference changes use `preferences_store()` after the adapter
checks its native menu context.

Forwarding defaults off. The outgoing body is the Hub's versioned status
envelope and includes actual sensor/actuator arrays. Asynchronous connection,
partial writes and reads use at most 512 bytes/eight calls per foreground
poll, a five-second request deadline and a 30-second failure backoff. No
blocking DNS/connect operation is permitted in a platform port.

## Verification

Run `make verify` here. Native integration tests exercise the real engine and
the unchanged HTTP state machine, including fragmented requests, malformed
JSON, exact catalogue capacity, hardware-specific validation, failed and torn
writes, corrupt/missing counters, pre-start reservation, complete history
capacity, restart recovery, all accessory channels, framebuffer revisions,
partial telemetry I/O and timeout behavior. ASan/UBSan additionally executes
the existing 1,111 HTTP parser cases, 16 streamed-asset cases and 4,500 mutated
command bodies through the common engine.

`tools/build_objects.py` verifies the archived dependency pins and compiles
the eight portable modules for either target. The checked reports under
`analysis/` are object-build evidence; each machine's modified profile owns
its real SDK composition and complete link. Native tests and target links do
not establish physical installation or brewing qualification.
