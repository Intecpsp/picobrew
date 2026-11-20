# Z119 Core commands and program startup

`core_commands.hpp` and `core_commands.cpp` reconstruct the state-dependent
Core command dispatcher and session startup. They call the separately recovered
ProgramRepository, coffee writer, boiler detector and heater controller; they do
not duplicate those modules. Runtime, UI, reset, storage and network actions use
the explicit CoreFlow service boundary until their composed implementations are
bound. No fallback implementation treats an absent service as success.

## Original address map

| Original function | Recovered behavior |
| --- | --- |
| `0x4011d32c` | State-dependent routing of the 55 command IDs |
| `0x4011d010` | Program lookup, metadata reset/copy, mode selection and Starting transition |
| `0x400f0480`, `0x400e69cc` | Coffee payload extraction, builder result and ProgramLoaded event |
| `0x400ee254` | Persist boiler type, change ADC calibration and retune the PID |
| `0x400edc80`, `0x400edcb0`, `0x400d5fd4` | Configuration/factory reset, display power and restart order |
| `0x400f3f58`, `0x400e60b8` | Resumed-session flag and queued StartProgram for buffered ID 24 |
| `0x400f4a2c`, `0x400f4ab4`, `0x400d106c` | Runtime command construction and float-to-double temperature |
| `0x400d7df8`, `0x400ed15c`, `0x400ec52c`, `0x400ec360` | Actual repository lookup and built-in statistics used by startup |
| `0x400dd9f4`, `0x400dd708`, `0x400dd68c` | Boiler detection, fresh ADC poll and selected calibration |
| `0x400de27c`, `0x400d0094` | Boiler-specific PID gains and time/direction scaling |

The original Core command object is an ABI structure, including a vtable and
several nested payloads. The portable `CoreCommand` uses typed values instead.
Its original recipe wrapper starts at command `+0x1e0`; accessor `0x400e69cc`
checks the program-parameters flag and returns wrapper `+0xb8`, so the actual
parameters are at command `+0x298`. Manual-step parameters are at `+0x1a0`.
These offsets are fixture evidence, not required addresses in the rebuilt code.

## Observable behavior

Idle accepts recipe/backend requests, program starts, configuration changes,
network choices and maintenance commands. Starting accepts explicit session
mode, deferred Runtime start and cancellation. Running routes pause/stop/resume,
manual steps, settings and PicoStill actions. NeedsNetwork, Registering,
StandBy and Error each have their original narrower command sets. Commands
outside a state's supported set leave application state unchanged; diagnostic
formatting is outside this module.

A program start accepts Idle or Stopping. The original assertion on another
state logs and returns to its caller, so the function still continues; the
reconstruction records the assertion line and preserves that continuation.
An unknown program returns without resetting session metadata. A found program
resets the session, calculates statistics for a built-in program, then copies
buffer parameters/statistics and the selected record's type, unique ID and name.
The calculation uses the recovered repository rather than a second catalog.

Offline device mode forces Offline session mode. Otherwise program IDs 14,
18 and 19 are Offline; IDs 7 and 26 use the saved PicoStill session setting;
other IDs use Online. These are the original masks `0x000c4000` and
`0x04000080`, guarded by ID < 27. Azure use is false when the still-unresolved
program-source field at Core `+0x17b0` equals 3, or the configuration explicitly
disables Azure. The source retains the numeric field name rather than inventing
a backend meaning. Startup marks Runtime pending, transitions Core to Starting,
and then requests Azure session initialization in that order.

Coffee loading calls the accepted writer with the command parameters, current
pressure and the saved unit choice. Only a successful build emits ProgramLoaded.
A resumed session queues StartProgram with ID 24; an already occupied command
mailbox retains the earlier value, while the resumed-session flag is still set.

Boiler detection explicitly requests another raw ADC poll before evaluating the
calibrated temperatures. It does not substitute the last completed Core tick's
sample. Setting the boiler disables heater timers, writes configuration,
re-enables timers, updates channel 0's calibration and then retunes the PID.
Reset paths disable timers before storage reset; factory reset additionally
selects the factory partition before display power is removed and restart is
requested. Actual reset/partition APIs remain platform service boundaries.

Runtime commands preserve constructor defaults for fields irrelevant to the
selected command. For example, SetDuration sets duration only; it does not copy
a spare temperature, location or manual payload from the Core command. The
original temperature setter extends the incoming binary32 value to binary64.

## Validation and limits

All **1,662 original scenarios** match the native implementation across **40
executed original bodies**, **1,106 ordered service effects**, complete session
metadata, PID coefficients, command payloads and mailbox changes. The portable
fixture retains **310 original cases**. Additional native checks exercise the
occupied mailbox, missing Runtime endpoint and payload-constructor defaults.
Cases cover every state in 0–18 against all valid command IDs and invalid
sentinels, every program ID, online/offline/Azure combinations, boiler settings,
PID direction/sample periods, credentials, reset variants and detector errors.

`EmulateCoreCommands.java` extends the shared Core fixture instead of duplicating
Core layout and service handling. It executes the actual repository lookup and
static duration estimator during session startup. Previously validated coffee
builder and calibrated thermistor results are explicit boundaries in this Core
comparison; their full numerical/code-generation comparisons remain separate.
CoreCommand construction/mailbox and external backend/Azure/UI/hardware endpoints
are also declared boundaries. The harness records these calls rather than
claiming that the unrecovered subsystem was executed. The detector fixtures use
valid raw ADC samples and finite conversion inputs. Invalid numeric-domain
rejections in the portable numerical adapters retain their separately documented
scope.

`analysis/core-commands-validation.json` pins the original image, every executed
instruction export, owned source, harness, fixtures and relevant accepted
component reports. The accompanying CoreFlow run was replayed after the shared
fixture extraction and new command fields: its 1,672 cases and 17,442 ordered
service events continue to match. No physical board was contacted. These modules
are a checked source checkpoint, not a linked ESP32 image or installation claim.

```sh
python3 tools/validate_core_commands.py --prepare-input build/core-commands-input.json
# Run EmulateCoreCommands.java in the preserved project with -noanalysis -readOnly;
# arguments are absolute input JSON and output JSONL paths.
python3 tools/validate_core_commands.py \
  --original-jsonl build/core-commands-original.jsonl \
  --export <expanded-original-export> --write-evidence
python3 -m unittest discover -s tests -p test_core_commands.py
```
