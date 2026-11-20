# Z119 Head Wi-Fi and Azure events

`head_network_events.hpp/.cpp` recovers Wi-Fi handler `0x401094c8`, Azure
handler `0x40105fbc`, and group-session menu body `0x40105e50`. It uses the
existing localized catalog and quantity-conversion implementation. Its
constructor binds a shared `ProgramParameters` reference so cloud coffee
updates can directly affect the same parameters used by numeric inputs.

## Wi-Fi dispatch

Core states Running (14), Stopping (15) and Error (16) suppress every Wi-Fi UI
event. Other states use the following original rules:

| Event | Recovered behavior |
| --- | --- |
| Error | Reset the screen data, append the event and error enum labels, then show localized title 56 with the original vertical-scroll descriptor |
| ScanStarted | Show localized title 38 with message arguments zero and one |
| ScanCompleted | Present the complete three-field event, except while Core is ConnectingWiFi (2) |
| Other | No UI action |

All three languages and the catalog's invalid-language fallback are retained.
Unknown error values use the original enum's invalid label. The source does not
replace the scan-results service: its SDK reads, filtering, duplicate removal
and menu population remain a separate child implementation.

## Azure events and group-session choices

`TimeoutStartProgram` shows the home screen. `ExitStartProgram` first posts
`AzureSessionDiscard` (47), then shows home. `DisplayStartProgram` resets the
screen data and builds the original group-session choice. Other event types
produce no action.

| Program ID | Added start command |
| --- | --- |
| 1 | StartRinse |
| 2 | StartDrain |
| 3 | StartRack |
| 4 | StartCirculate |
| 27 | StartChill |
| 5 | StartSousVideWithoutKeg |
| 6 | StartSousVideWithKeg |
| 26 | StartPicoStillBoilPoint |
| 7 | StartPicoStillManual |
| 12 | StartClean |
| 13 | StartPicoPaleDemo |
| 14 | StartValveTest |
| 15 | StartPumpsTest |
| 16 | StartAlarmTest |
| 17 | StartChillTest |

Those programs then receive `GroupSessionExit` and the original “Group Session”
title and “Start program?” prompt. Unsupported program IDs still receive the
exit choice and prompt, matching the diagnostic-only OEM default.

Coffee IDs 20 through 23 instead receive `CoffeeProgramLoad`. Their shared
parameters are updated in the original order: program ID; water converted to
liters; intensity value tagged as Intensity; duration value tagged as Second;
temperature converted to Fahrenheit. ABV, IBU and the resumed-session flag are
retained. The coffee title is “Group Session” and its message is “Coffee Program
loaded”. The source preserves the original one-line body and presentation
arguments.

An incompatible water or temperature quantity records assertion line `0x7d`,
uses the original zero fallback, and continues every subsequent parameter
assignment and screen operation. Both invalid quantities therefore produce
zero liters and zero Fahrenheit, with the copied intensity and duration still
present. The comparison executes the original quantity bodies and assertion
helper through its return rather than assuming their conversion output.

The assertion helper at `0x400db014` logs, increments its diagnostic counter,
delays 200 ms, and returns at `0x400db04c`; it does not halt the caller. An older
fixture stopped at the helper and consequently omitted the continuation in 96
incompatible-unit cases. That fixture has been replaced by complete traces.

## Service boundary

The required `ResetScreen` action means the complete original Head screen-data
reset (`0x400d1e60`), including its text/menu storage and language. Append-line,
append-command, display-layout, scan-results, home-screen and Core-post actions
also require concrete services. There are no successful default no-ops.
Missing or failed services stop subsequent operations. Action text pointers
are valid for the call; services must copy values they retain asynchronously.

The API receives consumed events. The oracle separately executes both original
mailbox reads, the Azure payload copy, CoreCommand construction, group-session
wrapper and title lookups. Screen reset/append/presentation, home, scan-result
generation, Core delivery, ROM memory operations, diagnostic output/delay and compiler
arithmetic remain explicit boundaries. A complete application and physical
display/network operation are not established by this module.
The prescribed clock samples do not advance for diagnostic output or the
assertion delay; the owned source retains assertion state without emulating
the diagnostic counter or blocking delay.

## Validation

**1,444 original-instruction cases** match ordered actions, translated title
bytes, all selected and retained parameter bits, and assertion state. Coverage
includes all Core states, all Wi-Fi/Azure event types and invalid values, all
program IDs, language fallback, every quantity-unit pair for coffee,
liter/gallon and Celsius/Fahrenheit inputs, incompatible-unit zero fallbacks
and completed presentation,
and nonfinite time/intensity bit copies. Portable service tests additionally
verify shared-state binding and failure ordering.

The portable fixture is deterministic gzip with `mtime=0`. Decompression
reproduces the original oracle JSON **byte for byte**. The report retains both
the original uncompressed SHA-256 and compressed-file SHA-256; compression does
not replace or weaken original evidence identity.

```sh
python3 tests/run_head_network_emulation.py \
  --project <byte-verified-z119-project> \
  --ghidra-home <pinned-ghidra> --java-home <pinned-jdk> \
  --output build/head-network-original
python3 tools/validate_head_network.py \
  --original build/head-network-original --write-evidence
python3 -m unittest discover -s tests -p test_head_network.py
```
