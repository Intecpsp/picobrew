# Standalone family controls

The Hub reads the authenticated device identity, capabilities, command argument
descriptions and supported settings before presenting controls. Adding or opening
a device performs reads. Commands require a user action, retain false/zero
arguments, and use the existing Hub confirmation and idempotency receipt path.
An uncertain appliance write is never retried automatically.

| Family | Controls and observations |
| --- | --- |
| C/S/Pro | Existing monochrome front panel, dial, local recipes, advertised native programs, stop latch, preferences and retained history. C keeps its explicit display geometry and separate firmware package checks. |
| Generic Z-series | Actual 128 × 64 grayscale panel, dial, local canonical recipes, native programs and advertised pause/resume. Named temperature, pressure and actuator readings remain available. The identity does not establish a Z1/Z2 mapping. |
| PicoStill | Native start, essential-oils, cleaning and stop; explicit pump state and fan channel/level; every reported temperature, pressure and actuator reading. No recipe catalog, heater controls or front-panel view is invented. |
| PicoFerm | Local fermentation start/stop, immediate sampling, sampling interval and pressure calibration. The page shows temperature, native pressure and battery, without brew or heater controls. |

PicoFerm calibration requires an explicit confirmation after removing the unit
from its keg or adapter, removing its GRAY seal and fully depressurizing it.
Opening or cancelling this dialog sends no calibration command. Sampling
interval is an integer from 60 through 86,400 seconds. The default form starts
at 300 seconds; it is labeled as an entry value rather than an observed setting.

PicoFerm pressure has no established physical unit in the recovered OEM source.
Its named reading is explicitly `raw`; normalized `pressure_kpa` remains null.
The Hub does not infer a conversion. Other readings follow their declared units,
including Fahrenheit temperatures and the normalized PicoStill kPa channel.

## Z recipes and front panel

Hardware identity `z-series` uses the existing generic Z planning profile for
canonical recipes. Every step requires an explicitly assigned direct valve
from 1 through 6, and the device's 15-step and temperature/duration bounds still
apply. Archived native `z` and `zymatic` programs are blocked from direct
deployment: their 0–5 routing and location-6 pause grammar require an explicit
conversion. A profile alias cannot silently convert that routing.

The Hub assembles the four Z display pages under one adapter lock, pins their
revision and rejects inconsistent dimensions, offsets or data. A revision change
discards the entire frame and retries at most twice. The browser preserves all
16 gray levels, nibble order and upright row order. Existing mono orientation
and the Pico C logical viewport remain unchanged.

## Interrupted brews

When the device advertises checkpoint recovery, the machine page displays its
saved recipe, step or preparation phase, remaining step time and elapsed time.
A pending checkpoint blocks new programs until it is resumed or discarded.
An invalid checkpoint remains visible for discard even when it is unavailable
for resumption. Loading this page never resumes the machine.

Resume asks the user to verify the water, vessel and filter, explains that saved
progress can lag the interruption by up to 30 seconds, and sends the explicitly
confirmed `resume-interrupted` command. The device resumes its saved remaining
time and may return to preparation prompts. Discard has a separate confirmation
and sends only `discard-interrupted` to the firmware. Live-session pause/resume
and clearing a stop latch do not perform checkpoint recovery.

## History

The session journal preserves the legacy eleven fields and every appended
normalized, sensor and actuator field. Field names and label/unit metadata come
from the selected session's sample pages. Charts group compatible units and
label native raw values; the sample table and JSON/CSV exports preserve all
channels without filling missing readings with invented zeroes.

Firmware retains up to 4,224 rows across eight sessions. Page size can vary up
to 32; `next_offset` controls continuation. The importer bounds the total read,
rejects a schema change during a download, checks retained metadata again after
reading, and reuses a previously verified prefix only when the same retained
window still exists. Truncated or overwritten source history remains explicit.

Each session's `sample_interval_ms` is retained in Hub history and JSON exports.
Changing PicoFerm's sampling interval for a later session does not relabel an
older session. A changed interval invalidates an imported prefix, and malformed
intervals are rejected. Older firmware that omits this field remains supported
without inventing a cadence. The local device browser likewise exports the
selected session's interval instead of the newest session's setting.

## Verification boundary

`internal/device/family_test.go` covers authenticated protocol fixtures, exact
argument values, display paging, and named history decoding.
`internal/server/family_test.go` covers Hub confirmation/idempotency, identity,
schema persistence, precision-preserving export and append/ring synchronization.
`internal/recipe/z_canonical_test.go` checks direct-valve compilation and archived
grammar rejection. `web/tests/family-controls.spec.ts` exercises all three new
families in Chromium at desktop and mobile sizes using local API fixtures.

These checks use the actual server/browser source and local fixtures. They do
not contact hardware, establish safe physical installation, or claim stock
firmware compatibility. The server remains self-contained under `server/`;
building or running it does not read sibling firmware sources.
