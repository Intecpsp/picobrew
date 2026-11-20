# Z-series MSP controller protocol reconstruction

The buildable component in `src/msp_protocol.c` reconstructs the ESP32 side of
the auxiliary MSP controller protocol in **Z-series 0.0.119**. It includes packet
construction, CRCs, response/state decoding, command guards, polling, valve
commands, and the controller boot/program/setup sequences. It supplies an
explicit synchronous transport boundary rather than an invented board driver.

This is a manually typed behavioral reconstruction, not original vendor source.
It does not constitute a complete Z-series ESP32 firmware build. The auxiliary
controller's own embedded machine code has not been reconstructed into source.
No controller, heater, pump, valve, I2C bus, or physical machine was operated by
these tests.

## Exact evidence

All addresses below are ESP32 virtual addresses in the preserved file
`../../oem/upstream/zseries_0_0_119.bin`, SHA-256:

```text
d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27
```

`tests/fixtures/msp-original.json` contains results from executing **24 original
function bodies**, with the byte ranges and hashes of every executed function.
`analysis/msp-protocol-validation.json` records the exact Ghidra/JDK distributions,
installed analysis files, generator hashes, fixture hash, and execution bounds.
The generator verifies every mapped program segment against the original image
before executing any function. The normal native tests recheck the original
function bytes, generator hashes, and fixture hash.

| Original address | Recovered behavior |
| --- | --- |
| `0x400d1f4c` | Begin an I2C transmission to the configured controller address |
| `0x400d1f60` | Write a bootloader packet and end the transmission |
| `0x400d2000` | Update a 16-bit CRC with one byte, using eight bit steps |
| `0x400d2030` | CRC of bootloader command/address/data bytes |
| `0x400d6108` | Delay, then change the controller state ID |
| `0x400d6164` | CRC of the embedded controller image's application range |
| `0x400dbcec` | Require the application state and an idle valve, except for state queries |
| `0x400dbd2c` | Request bytes; yield until the requested count is available |
| `0x400dbd94` | Read and decode a fixed nine-byte application response |
| `0x400dc050`, `0x400dc0a4` | Basic application command, without/with the state guard |
| `0x400dc0dc`, `0x400dc104` | Switch to boot; request application version |
| `0x400dc1c4` | Periodic state query |
| `0x400dc2f8`, `0x400dc528` | Application commands with an 8-bit or 32-bit value |
| `0x400dc5cc`, `0x400dc5e8` | Read a bootloader result; send a simple bootloader command |
| `0x400dc630` | Program an image segment in chunks of at most 16 bytes |
| `0x400dc8d8` | Tell the bootloader to start the application |
| `0x400dc984` | Move the valve to a validated position |
| `0x400dcaa8`, `0x400dcb84` | Erase the application; program the image's segments |
| `0x400dcdc0` | Detect controller state, verify boot version, start/update app, verify app version |

The debug-command registry at `0x400ffc88` identifies the pump, fan, servo, and
valve command names. Its address-taken wrappers include functions the initial
automatic analysis did not turn into named function objects. For example,
the wrapper at `0x400dc1f8` sends `0x14` and is registered as “Hex pump OFF.”
These small wrappers were also inspected with the matching official Xtensa
GCC 5.2 toolchain's `objdump`, starting at each exact entry address.

## Transport and application packets

The original transport is **Arduino TwoWire/I2C**, not UART. The helper at
`0x40125a28` identifies `Wire.cpp` and `requestFrom` in its diagnostics; its
request/receive-buffer layout and the begin/end wrappers match the pinned
Arduino ESP32 1.0.1 `libraries/Wire/src/Wire.cpp` source. `0x400dbd2c` requests
the count and repeatedly calls `available()` with `yield()` while too few
bytes are present. The original code does not check `endTransmission`'s return
value and does not impose a receive deadline.

Application packets are:

| Command value type | Bytes on the wire |
| --- | --- |
| No value | `08 01 command AD DE` |
| 8-bit value | `08 02 command value AD DE` |
| 32-bit value | `08 05 command value[0] value[1] value[2] value[3] AD DE` |

The value is little endian. `AD DE` is a **constant trailer**, not a computed
CRC. After transmitting, the code waits the configured response delay and
reads exactly nine bytes: a response type plus eight payload bytes. The ESP32
parser does not validate a response checksum.

Response type `1` copies payload bytes 0 and 1 to the major/minor version.
Type `2` updates the status fields below. Types `0`, `0x51`, and unrecognized
types leave the state fields unchanged, apart from diagnostic logging.

| Original state offset | Status response interpretation |
| --- | --- |
| `+0x04` | State ID; unaffected by the response parser |
| `+0x08` | Payload byte 0 masked with `0x07`; bit 0 is bare PCB, bit 1 is use-servos |
| `+0x09` | Payload byte 0 masked with `0xf0`; pump flags remain unshifted |
| `+0x0a` | Payload byte 0 masked with `0x08`; the fan flag remains `0` or `8` |
| `+0x0b` | Payload byte 1: valve position |
| `+0x0c`, `+0x0d` | Payload bytes 2 and 3; physical meaning still unresolved |
| `+0x10` | Payload bytes 4..7: little-endian 32-bit error flags |
| `+0x14` | Valve-moving flag, cleared by every type-2 response |
| `+0x15`, `+0x16` | Application major/minor version, unaffected by type 2 |
| `+0x17` | Full payload byte 0, retained for change logging |

The portable structure omits the original C++ vtable and ABI padding. Its two
unresolved bytes remain `status_byte_2` and `status_byte_3` rather than receiving
speculative hardware names.

The command guard requires `AppRunning` (`2`). While the valve-moving flag is
set, only state query `0xfd` is permitted. Valve positions must be `1..9` and
produce commands `0x14 + position`. The moving flag is set **after** processing
the command response. Increment/decrement commands `0x1f`/`0x20` use the same
post-response flag behavior.

## Bootloader packets and image handling

The observed bootloader format has a **one-byte body length**:

```text
80 length command [address:3] [data] crc_low crc_high
```

The CRC starts at `0xffff`, uses polynomial `0x1021`, processes bits most
significant first, and has no reflection or final XOR. It covers only the
command/address/data body, excluding the framing bytes and the CRC itself.

| Command | Body | Response behavior |
| --- | --- | --- |
| `0x19` | Command only | Read one version byte; accepted version has high nibble `0xa` |
| `0x1c` | Command only | Start app; **no acknowledgement is read** |
| `0x10` | Command + 24-bit little-endian address + 1..16 data bytes | Read one result; zero means success |
| `0x12` | Command + 24-bit little-endian address | Read one result; zero means success |

Programming increments the full 32-bit source address, but only its low 24
bits go onto the wire. It stops at the first nonzero result, with no retry.
Erasing sends 30 descending addresses: `0xfbff`, `0xf9ff`, through `0xc1ff`,
one per 512-byte segment. It stops at the first error, where the OEM asserts.

The descriptor at `0x3f425ddc` contains four segments for the production MSP
application, whose expected version is `1.0`. The descriptor at `0x3f425578`
contains five segments for a built-in test application, whose expected version
is `255.255` (`0x400dd158`). `analysis/msp-protocol.json` records every original
segment address, size, file offset, and payload hash.

The image CRC covers MSP addresses `0xc002..0xfbff`, in increasing order, filling
unmapped bytes with `0xff`. The first matching descriptor segment wins if
segments overlap. The original instruction bodies produce `0x5fdb` for the
production image and `0xc60a` for the test image. The production value matches
the assertion in `0x400dcdc0`. The excluded two-byte segment at `0xc000` holds
the same value in **big-endian byte order** (`5f db` / `c6 0a`), distinct from the
little-endian CRC trailer on bootloader packets.

## Startup and retained OEM behavior

Startup checks the production image CRC, then sends an unguarded application
version request. Five leading `0x51` bytes identify the bootloader; response
type `1` identifies the application. A lone `0x51` is insufficient. An unknown
response leaves the state `Unknown`.

When an application is detected, the code switches it to the bootloader. It
checks the boot version, starts the app, and reads the app version. A missing
version response or any version other than `1.0` triggers an update of the
embedded image, followed by another start/version request. State transitions
wait their configured delay **before** writing the new state ID. Version
responses alone do not transition state.

Two less desirable OEM behaviors are deliberately preserved as evidence:

- Polling uses `next_poll < now`, a strict unsigned comparison; its rollover
  behavior has not been silently repaired in this reconstruction.
- During setup, the OEM logs a programming failure or a failed second boot
  version probe and still tries to start/read the app. The typed reconstruction
  retains that sequence. An erase failure corresponds to a fatal OEM assertion
  and stops the reconstructed operation.

The portable interface returns explicit negative results where the OEM calls
its fatal assertion helper. This is an intentional API adaptation, not a claim
that the OEM returned these values. Tests map these boundaries to the captured
assertion lines (`0x70`, `0x75`, `0x340`, `0xff`, `0x2f0`, `0x2f1`). Buffer-capacity
validation is likewise a new portable API boundary. Valid command sequences,
wire bytes, state writes, and I/O order are compared directly with original
executions.

## Validation and the CRC decompiler defect

The original byte-CRC function contains `LOOP a9` with `a9 = 8` at
`0x400d200e`. Ghidra 12.2's generated C loses that loop and performs only one
bit step. Copying that C would produce a broken protocol.

The focused harness executes the original loads, shifts, XORs, branches, and
stores, while explicitly interpreting that reviewed LOOP's eight iterations.
It normalizes `ENTRY`, `CALL8`, and `RETW` register-window boundaries and supplies
I2C, delay, clock, and diagnostic callbacks. It is **not a full CPU or peripheral
emulator**. Startup cases reuse the embedded-image CRC results only after both
images were independently executed through the original CRC routines.

There are **5,553 captured cases**, compared with compiled C in 13 native test
groups: all 256 input bytes for eight CRC seeds; all response types and status
flag values; all application commands for each supported value width; command
guards; bootloader packets; chunk/address/error boundaries; erase stop points;
valve validation; polling boundaries; both image CRCs; and 11 startup/update
scenarios including errors. All groups pass. The component also compiles with
the matching official Xtensa GCC 5.2 compiler; an object file is not a complete
linked firmware image or physical validation.

The fixtures deliberately use address `0x48`, poll interval `41`, transition
delay `17`, and response delay `23` to expose parameter use. **Those values are
test inputs, not recovered board defaults.** Pin selection, bus initialization,
timing defaults, the two unresolved status bytes, and the auxiliary controller's
own source remain work for the complete port.

Run the ordinary host checks from `reverse-engineered/`:

```sh
python3 -m unittest discover -s tests -p 'test_msp_protocol.py' -v
```

To regenerate the original-instruction evidence, use a preserved Z119 project
from `tools/firmware_analysis/` and the pinned Ghidra/JDK distributions. Choose
a new output directory; the runner snapshots its script, verifies the pinned
tools, runs read-only with bounded execution, and records the exact hashes:

```sh
python3 tests/run_msp_emulation.py \
  --project /path/to/z119-project \
  --ghidra-home /path/to/ghidra_12.2_PUBLIC \
  --java-home /path/to/jdk-25.0.5+8 \
  --output build/msp-original
```

The output's `msp-original.json` corresponds to the committed test fixture;
`msp-protocol-validation.json` corresponds to the analysis provenance file.
No automatic-analysis labels or successful test counts establish compatibility
with a particular Z1/Z2 physical board.
