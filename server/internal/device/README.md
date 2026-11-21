# Device adapters

This package separates the hub from appliance protocols. It supports the
standalone REST API v1 implemented by `picobrew-pico-pro-firmware`, and an
independent in-memory simulator. A Z, Zymatic or Pico C **simulator** does not
establish support for controlling that model's stock firmware. There is no stock
PicoBrew cloud-protocol adapter in this package.

## Physical standalone connection

`NewPico(baseURL, key)` accepts an explicitly configured HTTP or HTTPS endpoint
and the device's 64-character hexadecimal API key. Discovery requires API version
1 and `mode: "standalone"`; commands must also be advertised by that device.
Firmware semantic versions are reported independently of the API version.

The adapter accepts authenticated C/S/Pro, generic `z-series`, `picostill` and
`picoferm` identities. It verifies matching model/family and device UID rather
than treating an accessory as a brewer. Nested capability flags override legacy
defaults; the original C/S/Pro discovery shape remains supported. Command
descriptors supply allowed/required fields, and settings advertise their own
field subset. Explicit false and zero arguments are preserved.

The endpoint must be a host with an optional port, without a username, password,
path, query or fragment. Private LAN addresses are supported. Loopback,
unspecified, multicast and link-local addresses, localhost names and the
`100.100.100.200` metadata address are rejected. DNS results are checked at each
connection, then the validated numeric address is used for that connection.
HTTP proxy environment variables are ignored. The private loopback override is
used only by the package's local protocol tests.

One adapter serializes all requests to its device. Waiting for the serialization
lock respects context cancellation; each HTTP request has a three-second timeout.
The caller's context can also bound a complete inventory or recipe-write operation.
Connections close after each response, redirects are rejected, JSON writes use
an explicit content length, and no mutation is automatically retried. Requests
are limited to 4,095 body bytes; responses are limited to 64 KiB. Error messages
are bounded and strip the configured address and credential. HTTPS verifies the
server certificate through the normal system trust store.

| Adapter operation | Appliance endpoint and behavior |
| --- | --- |
| Discover | `GET /api/v1`; API version, capacities, command support |
| Status | `GET /api/v1/status`; temperatures remain Fahrenheit regardless of display preference |
| Display | `GET /api/v1/display`; legacy mono geometry or revision-pinned Z grayscale pages, assembled under the same request lock |
| Recipes | `GET /api/v1/recipes`, then each full recipe; this happens only on an explicit inventory request |
| PutRecipe | `PUT /api/v1/recipes/{id}`, followed by exact GET readback under the same serialization lock |
| DeleteRecipe | `DELETE /api/v1/recipes/{id}`; succeeds on 204, with the firmware enforcing idle-state rules |
| Command | `POST /api/v1/commands`; requires a valid payload, advertised support and a matching 202 acceptance response |
| Settings / UpdateSettings | `GET` / `PATCH /api/v1/settings` |
| Telemetry / UpdateTelemetry | `GET` / `PUT /api/v1/telemetry`; the latter requires all three configuration fields |
| History | `GET /api/v1/history` |
| HistoryPage | `GET /api/v1/history/samples/{offset}?id={id}` |

`PutRecipe` returning nil means readback matched the complete intended native
recipe, including its ID. It does not start a brew. A successful command means the
firmware accepted the command; preparation prompts and physical interlocks still
apply. Dedicated pause/resume and accessory actuator controls are available only
when explicitly advertised. Stop-latch behavior belongs to the device; clearing
a supported latch does not resume an interrupted operation.

Optional `status.recovery` metadata preserves an interrupted brew's recipe,
step, saved remaining time and elapsed time. Absent/null metadata grants no
recovery feature. Preparation checkpoints may use step -1 and an empty step name.
Invalid checkpoints stay visible for discard even when `available:false`.
Metadata is bounded before it reaches the Hub UI. Only an advertised
`resume-interrupted` command with `confirm:true` can request resumption;
`discard-interrupted` sends no extra device arguments. The Hub displays the
water/vessel/filter review and the possible 30-second progress lag, and never
resumes on discovery, status refresh or history import. Both actions use the
existing explicit confirmation and command-receipt path.

For display rendering, raw pixel `(x, y)` is bit `y & 7` of byte
`x + (y / 8) * 128`. Rotate the decoded framebuffer 180 degrees for the upright
front-panel view unless an explicit rotation is supplied. This transformation
belongs to the presentation layer. Z uses row-major four-bit grayscale, with
even x in the high nibble and odd x in the low nibble; its 4,096 bytes arrive in
four pages pinned to one revision. A display revision conflict retries the
whole frame at most twice. No malformed or partial frame is returned. Devices
without `front_panel` do not receive display requests.

### Mutation outcomes

`device.Error` exposes `StatusCode`, `Code`, `Message` and `UnknownOutcome`.
Invalid input and ordinary device rejections are definite failures. A lost write
response, server-side write failure, unexpected write status or failed recipe
verification may mean the appliance changed despite the error. These cases set
`UnknownOutcome`. Read the device's current state or recipe back before deciding
on another action. A hub idempotency key cannot make an unacknowledged appliance
write intrinsically idempotent.

## Retained brew history

The physical recorder retains up to eight brew entries with a shared capacity of
4,224 samples. `RecordCount` counts currently retained rows, including a RAM tail;
`PersistedRecordCount` describes the committed subset. History IDs are numeric
32-bit IDs and must not be confused with the string `Status.SessionID`.

Pages contain at most 32 rows and start with 11 fixed fields: elapsed milliseconds; wort,
thermoblock and target temperatures; step seconds; phase; controller status;
compartment; current step; flags; and device state. Unavailable temperatures are
null. New firmware appends normalized readings and named sensor/actuator fields.
Each page carries that session's stored field names and metadata; the current
catalog cannot overwrite an older session's schema. The adapter validates page identity, continuation, field counts, numeric
bounds and ordering within the page. Raw state codes are preserved. It does not
invent wall-clock timestamps: the appliance's stored records provide elapsed
time, not a trustworthy brew start time.

The recorder can change while pages are read. A removed history ID produces 409;
the caller must compare metadata before and after a download, retain partial
data honestly and retry a fresh snapshot later. The hub's history synchronizer
implements that policy and saves the schema with the archived session. A fully
verified append-only import can reuse its existing prefix; a changed ring origin
forces a fresh read. Every named channel survives JSON and CSV export. Old
brews can disappear before reconnection, and a
restart can lose an uncommitted tail. Already missing samples cannot be recovered
from this API. Exporting or archiving history does not delete it on the device.

## Simulator

`NewSimulator(id, model)` creates isolated state without opening network
connections. Its firmware version is `simulator`, device UID is prefixed with
`simulator:`, samples use source `simulation`, and it advertises no persistent
history or outbound telemetry forwarding. The model label is descriptive.

The simulator has no OLED framebuffer. Each simulator supports stored recipes, settings, encoder input,
REST-started accelerated brews, maintenance completion, stop latching and
retained history. Time runs at 60 simulated seconds per real second. History is
volatile and has the same eight-entry / 4,224-row shared limits. The `demo-pro`
and `demo-z` fixtures start in different simulated brew phases; `demo-s` starts
idle. All fixtures are independent. No simulator command controls a real brewer.

## Verification

`go test -race ./internal/device` exercises local HTTP fixtures, request framing
and authentication, recipe readback, mutation ambiguity, serialization,
cancellation, redirect rejection, bounded/redacted errors, history schema checks
and simulator lifecycle/isolation. These tests do not contact an appliance and
do not substitute for physical protocol or brewing validation.
