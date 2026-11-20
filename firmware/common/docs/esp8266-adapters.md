# ESP8266 standalone storage and telemetry

The adapters in `platform/esp8266` use the selected Arduino ESP8266 2.4.1 SDK,
its bundled SPIFFS and lwIP 1.4, and Xtensa LX106 GCC 4.8.2. The complete source
and archive identities are pinned in `dependencies/esp8266-adapters.json`.
These are new shared standalone services, not claims about OEM binary parity.

## Filesystem ownership

`picobrew::standalone::esp8266::SpiffsStorage` owns its raw SPIFFS mount and the
common `VerifiedStorage` transaction. Its default geometry comes from the actual
`_SPIFFS_start`, `_SPIFFS_end`, `_SPIFFS_page` and `_SPIFFS_block` linker symbols.
The explicit geometry constructor also supports a selected partition and tests.
One foreground owner must serialize calls; Arduino's global `SPIFFS` must not
mount the same partition concurrently.
The SDK's 2 MiB linker layout reserves `0xFB000` bytes: SPIFFS uses its 125 whole
8 KiB logical blocks and leaves the final 4 KiB remainder outside that block
count. Validation accepts this evidenced layout and still requires physical
erase alignment.

`begin()` only mounts. A blank, corrupt or unreadable filesystem returns failure
without automatic formatting. Installation must provision a suitable filesystem
or expose an explicitly requested initialization operation. The historical
Arduino filesystem wrapper automatically formats after a failed mount and loses
several error distinctions, so this adapter calls the pinned raw SPIFFS API and
checked SDK flash HAL directly.

The `binding()` method supplies the shared engine's `Storage`. Raw stat/open
results preserve `Missing` separately from `Error`; transfers return their actual
counts; seek, flush, close, rename and remove results are checked. The transaction
uses exact temporary/final readback and recoverable backups as documented in
`verified-storage.md`. If raw close fails, SPIFFS may leave its descriptor open;
the adapter invalidates the mount to release descriptors and reports failure.
A subsequent explicit `begin()` can remount. It never promotes that temporary
file after a failed close.

## Nonblocking telemetry

`AsyncTelemetry::binding()` supplies the engine's `TelemetryIo`. It uses
`dns_gethostbyname`, `tcp_connect`, `tcp_write` with `TCP_WRITE_FLAG_COPY`, and raw
TCP callbacks. Numeric IPv4 hosts bypass DNS. No public method waits or loops
for DNS, a connection, writable capacity or incoming data. The engine retains
responsibility for deadlines, retry policy and the full outgoing HTTP frame.

The DNS API has no cancellation operation. A bounded pool of `DNS_TABLE_SIZE`
tickets keeps a canceled callback context alive until its SDK callback arrives.
Closing or destroying a connection detaches its owner from the ticket; an old
answer cannot attach to a newer connection or access a destroyed owner. Pool
exhaustion returns failure immediately. Numeric addresses remain usable.

Writes accept at most 512 bytes per call and copy them into lwIP. A transient
`tcp_output` failure after copying still returns the accepted byte count, so a
retry cannot duplicate already queued bytes. Foreground polls and the SDK poll
callback retry output without waiting. Fatal errors detach callbacks and abort.

Receive retains one SDK pbuf chain. It acknowledges the TCP receive window only
as the foreground consumes bytes, and returns `ERR_MEM` for another chain while
one is retained; lwIP owns and redelivers that refused chain. Reads copy at most
512 bytes, drain pending data before EOF, and release accepted chains exactly
once. Close detaches every callback before requesting a close, with immediate
abort if lwIP cannot queue the close. There are no live cloud requests in tests.

## Evidence

`analysis/esp8266-adapters-validation.json` records source identities and a
32-bit native execution using the repository's pinned i386 runtime. The storage
suite runs the actual five SPIFFS C sources against a deterministic NOR flash
model with bitwise-AND programming and checked erase geometry. It covers mount
preservation, offsets, empty files, replacement, remount persistence, interrupted
rename recovery, and real flash read/write failures. Its 62 checks execute
3,684 HAL reads, 1,243 writes and 64 erases against a 256 KiB flash image.

The transport suite executes the owned source against the unmodified SDK lwIP
public headers with an explicit native architecture header and controlled SDK
callbacks. Its 261 checks cover asynchronous DNS/connect, cancellation and owner
destruction, slot exhaustion, partial/copied writes, deferred output, fatal
errors, callback abort conventions, receive backpressure, pbuf chains and EOF.
All 16 created pbuf nodes are released. The fake boundaries are the network
services themselves; this is not a live-network test.

`analysis/esp8266-adapters-build.json` records target object compilation of the
three owned modules and six actual SDK SPIFFS/HAL modules. All 16 referenced
lwIP API symbols are verified in the pinned `liblwip_gcc.a`. Complete family
image linking, physical installation and hardware validation are separate.

The separate `spiffs-capacity-validation.json` drives the actual shared engine
through complete 4,224-sample histories, remounts and subsequent sessions. The
compact record encoding stores six bounded legacy values as bytes and omits
structurally unsupported actuator columns. This preserves the JSON values and
full retained history while recovering filesystem working space.

| Profile/model | Partition bytes | Records | SPIFFS used | SPIFFS logical free | Remount and next session |
| --- | ---: | ---: | ---: | ---: | --- |
| Ferm, exact ESP8266 linker geometry | `0xFB000` | 4224 | 535885 | 421429 | Pass |
| Still, historical ESP32 default geometry model | `0x16F000` | 4224 | 889795 | 484681 | Pass |
| Still, selected modified ESP32 reference model | `0x200000` | 4224 | 889795 | 1030606 | Pass |

Each completed run retains 358 filesystem objects; Ferm stores 429,299 payload
bytes and Still 727,757. The test reads the final history page both before and
after remount, then starts session 2 and writes another 25 samples, exercising
replacement-bank reserve after a full history.

The smaller Ferm-region Still stress run remains an explicit failure: it stops
at 3,644 samples and cannot complete engine initialization after remount. Its
retained last page remains readable. That unsuitable partition is not a supported
Still layout. The SDK's GC working-space requirement can exhaust before its
logical free-byte counter reaches zero.

The ESP32 model uses the pinned ESP8266 SPIFFS implementation with the actual
ESP32 configuration: 4 KiB blocks, 256-byte pages, GC limit 10, four metadata
bytes, filesystem-length magic and unaligned object tables. The exact 49-byte
object header is checked against the actual ESP32 SDK DWARF and target compiler
in the separate formatter evidence, `spiffs-image-validation.json`. This is a
sizing model; it does not execute the ESP32 filesystem binary or establish the
OEM partition map. Installation images and their explicit reference layouts are
documented in `spiffs-images.md`.

`spiffs-capacity-initial.json` preserves the earlier failed sizing experiment
before compact encoding. Its ESP32 model also inherited the ESP8266 object-table
alignment (50-byte header), a limitation explicitly marked in that historical
report. The current capacity evidence corrects that layout and supersedes its
capacity gate.

```sh
python firmware/common/tools/build_esp8266_adapters.py \
  --arduino /path/to/esp8266-2.4.1 --toolchain /path/to/xtensa-lx106-elf
python firmware/common/tools/validate_esp8266_adapters.py \
  --arduino /path/to/esp8266-2.4.1 --i386 /path/to/i386-native --write-report
PICO_ESP8266_SDK=/path/to/esp8266-2.4.1 \
  python -m unittest discover -s firmware/common/tests -p 'test_*storage.py' -v
PICO_ESP8266_SDK=/path/to/esp8266-2.4.1 \
  python -m unittest discover -s firmware/common/tests -p 'test_esp8266_adapters.py' -v
```
