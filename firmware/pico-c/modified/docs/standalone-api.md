# Pico C standalone API v1

The complete wire schema is [openapi.json](openapi.json). This API implements
the same local operating model as S/Pro on top of the recovered C application:
recipes, session identity, preferences, history and execution stay on the
appliance. The Hub is optional. See the [component guide](../README.md) for
build variants, source provenance, tests and the physical qualification boundary.

## Identity and display geometry

Discovery, health and status identify the compiled target as:

```json
{"model":"pico-c","model_name":"Pico C","model_family":"pico-c","display_variant":"ssd1309"}
```

`display_variant` is explicitly selected at compilation (`ssd1325` or
`ssd1309`). It describes the running image, not detected hardware. A software
claim alone cannot prevent the wrong initial flash. The Hub pins the device UID,
checks discovery/status consistency, identifies the model automatically and
keeps independent controller verification separate for firmware applicability.

`GET /api/v1/display` returns `width`, `height`, `rotation`, `display_variant`,
`storage_bytes`, `transfer_bytes`, `format` and `data`. `data` contains 1,024 raw
bytes as 2,048 lowercase hexadecimal characters; `format` is `page-lsb-hex`.
Raw pixel `(x,y)` is bit `y & 7` in byte `x + floor(y/8)*128`.

| Variant | Width × logical height | Rotation | Storage / transfer |
| --- | --- | --- | --- |
| SSD1325 | 128 × 64 | `2` (180°) | 1,024 / 1,024 bytes |
| SSD1309 | 128 × 32 | `0` | 1,024 / 1,024 bytes |

Render only the logical viewport, then apply rotation. The hidden SSD1309 tail
is retained raw storage, not a second display. Both local and Hub websites use
these fields. Older S/Pro responses without these fields retain their existing
128 × 64 / 180° interpretation in the Hub.

## Authentication and request limits

The local server listens on HTTP port 80. API requests use
`Authorization: Bearer <device-key>`. The 64-hex-character per-device key lives
in `/local/api.key`; it is never compiled into an image or forwarded to a
telemetry receiver. The website stores the entered key in that browser tab's
session storage. The API fails closed while the key or application is not ready.

[Provisioning](standalone-provisioning.md) creates a private local key file but
does not provide a qualified C installation procedure. HTTP is intended for the
local trusted network; this firmware does not add HTTPS, internet discovery,
a public relay, arbitrary outbound URLs or remote shell access.

The HTTP implementation uses bounded request/response storage and one
foreground service. It rejects unsupported framing and malformed JSON, closes
responses explicitly and does not run appliance workflows inside request
handling. Inputs and uncertain writes are tested through the actual C modules.

## Routes

| Route | Purpose |
| --- | --- |
| `GET /api/v1` | API/version/model/display identity, limits and supported commands |
| `GET /api/v1/health` | Readiness, authentication state and uptime |
| `GET /api/v1/status` | Current control state, temperatures, recipe, encoder and stop reason |
| `GET /api/v1/display` | Raw framebuffer and logical geometry |
| `GET /api/v1/recipes` | Local recipe inventory and capacity |
| `GET /api/v1/recipes/{id}` | One complete recipe |
| `PUT /api/v1/recipes/{id}` | Validate and save a recipe while idle |
| `DELETE /api/v1/recipes/{id}` | Delete a recipe while idle |
| `POST /api/v1/recipes/restore-defaults` | Add missing starter recipes without replacing edited ones |
| `POST /api/v1/commands` | Queue a program, move/click the dial or operate the stop latch |
| `GET /api/v1/settings` / `PATCH /api/v1/settings` | Display units, language and detailed status |
| `GET /api/v1/telemetry` / `PUT /api/v1/telemetry` | Saved live-data receiver configuration and status |
| `GET /api/v1/history` | Retained session inventory and current recording metadata |
| `GET /api/v1/history/{offset}?id={id}` | Bounded pages from one retained session |
| `GET /api/v1/events` | Bounded volatile diagnostic events |

The schema defines required fields, allowed values and status/error responses.
There is no firmware installation, raw GPIO, calibration, Wi-Fi credential,
filesystem, API-key reset or cloud billing route.

## Recipes and recovery

The store has sixteen recipe slots and fifteen steps per recipe. IDs contain
1–20 ASCII letters, digits, `_` or `-`; names have the wire limits in the schema.
Each step has a checked compartment location (1–6), target, active minutes and
drain minutes. Targets are unheated (`0`) or 60–212°F. Active minutes are 0–599;
a zero-minute step heats to its target without an added hold. Drain minutes are
0–120. Routes remain explicit C compartment numbers; the server does not infer
plumbing from labels.

Eight starter programs retain the existing catalogue provenance in
[data/starter-recipes.json](../data/starter-recipes.json). A pristine store seeds
them once. Restore adds only missing recipes, preflights capacity and preserves
user edits. Recipes are ingredient/process programs, not proof that a physical
batch or ingredient setup has been tested on this C port.

Recipe, preference and recovery writes use checksums, readback and alternating
records. Interrupted writes preserve the previous committed state when one
exists; corruption and uncertain storage results fail closed. The API does not
silently erase a damaged store. Local test fixtures inject truncated writes,
retained flash tails, read/close failures and resets; these are not physical
power-loss qualification.

A restart offers recovery only when the saved checkpoint belongs to the same
unchanged recipe. Preparing or repeatedly viewing an offer leaves outputs off,
does not start a session and does not overwrite the checkpoint. Accepting it
continues the recovered C workflow; declining clears the saved offer. C's
maintenance program layout and timing remain C-specific, including its cleaning
steps without S/Pro's final timed drain.

## Commands and cancellation

Discovery advertises `brew`, `sous-vide`, `cold-brew`, `manual`, `rinse`,
`first-rinse`, `descale`, `circulate`, `drain-reservoir`, `drain-filter`,
`rack-beer`, `utilities`, `settings`, `stop`, `clear-stop` and `encoder`.
Programs are queued only from the main-menu boundary and dispatch after the
HTTP call returns. An explicit pending-command check prevents the C menu's
click initialization from losing a queued command. A physical encoder click
remains independent of that queue.

`stop` inhibits heater/pump outputs immediately and remains latched while the
workflow unwinds. `clear-stop` is a distinct idle-main-menu action; it never
resumes the cancelled operation. Filter removal during pumping trips the same
inhibition path without networking, allocation or UI work inside the ISR.
Foreground waits service bounded network work and observe cancellation,
including C marquee prompts, character editing and compartment draining.
Dedicated pause/resume is not advertised; use the existing front-panel workflow.

The standalone profile does not offer PicoStill or legacy factory recovery.
The firmware's brewing controller and recovered C hardware mappings remain the
execution path; the web interface cannot directly energize a raw pin.

## Hub connection and history

Adding an authenticated C device to the Hub discovers its model/variant and
pins its UID. The Hub saves and applies receiver **host**, **port** and **send
live readings** state when connecting or changing those values. Existing manual
destinations remain editable in the Hub. Firmware web settings show connection
status and do not duplicate the receiver form.

Forwarding remains optional, bounded and observation-only. Authenticated Hub
polling and history reads remain authoritative; forwarded data cannot authorize
commands. The receiver never receives the API key. See [telemetry.md](telemetry.md)
for transport, persistence and an optional standard-library receiver.

History retains up to eight sessions and 4,224 sample records, uses a 15-second
sampling cadence with 60-second persistence checkpoints, evicts whole older
sessions and reports gaps/truncation/storage failures. The website pages and
exports a specific session ID; it detects an inventory change rather than
combining data from different sessions. Maintenance does not create or evict a
brew recording.

## Update boundary and validation

Discovery reports `firmware_update:false`. The Hub can upload or fetch complete
C packages from a pinned GitHub commit, and checks model, explicit controller,
browser hashes, image vectors and RAM bounds. Marking a C package as matching
also requires a saved independent controller observation tied to that appliance
UID. Downloading a package performs no device operation.

The build emits a legacy OEM-format update envelope for analysis/compatibility
work, but this does not implement a safe standalone OTA installer. An initial
recovery/bootstrap, transaction ordering, rollback and physical qualification
remain separate. The [validation record](../analysis/standalone-validation.json)
records software evidence; no physical C flashing, boot or brewing is claimed.
