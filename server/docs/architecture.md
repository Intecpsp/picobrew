# Architecture

## Shared firmware contract

This Hub and all custom machine firmware belong to
`jeffreydwalter/picobrew-pico-pro-firmware`. The project target is to reverse
engineer Pico C, S/Pro, Z-family and Zymatic firmware and give each machine the
same standalone `/api/v1` contract and local browser workflow established by
S/Pro. Hardware-specific drivers, brewing routes and physical limits belong in
the corresponding firmware port and its advertised capabilities. The Hub then
uses that common contract for fleet management.

S/Pro and C now implement this standalone contract. C preserves its recovered
controller/UI and reports explicit SSD1325/SSD1309 geometry; its builds and
software tests do not establish physical qualification. Other model profiles
support planning and simulation until their firmware ports and Hub compatibility
are implemented and verified. Older cloud-replacement servers are reference
material, not a required component of this architecture. See the
[project roadmap](../../docs/project-roadmap.md).

## One owner of the control loop

The brewer firmware owns timing, heating, pumping, interlocks, preparation prompts and the stop latch. The hub queues the public operations that the firmware advertises. It does not synthesize low-level actuator commands or run a heating loop over Wi-Fi.

The browser talks only to the hub. The hub stores device credentials encrypted in SQLite and talks to each appliance on its private network. Visible Hub tabs fetch the cached fleet snapshot every three seconds. API clients can also subscribe to the fleet event stream. One disconnected appliance does not block other appliances. There is one polling worker per enabled device.

## Device transport

Each physical adapter has a context-aware serialization gate because the Pico HTTP implementation handles one connection at a time. Requests use a bounded three-second HTTP timeout, connection-close semantics, explicit Content-Length, and the firmware's 4095-byte JSON ceiling. Redirects are refused, and neither proxy environment variables nor automatic write retries can redirect/replay a credential-bearing mutation. Hostnames are resolved and checked before dialing; loopback, link-local, multicast and known metadata addresses are not accepted as physical brewers. Private LAN and VPN addresses are allowed.

A command has an owner-supplied idempotency key and a persistent receipt written before delivery. A duplicate identical request returns the prior receipt. Reusing the key for different data is a conflict. A definite firmware rejection is recorded as rejected. A transport failure after dispatch is an unknown outcome, never a reason to resend automatically. After process interruption a pending receipt remains evidence of an uncertain attempt.

Recipe deployment performs a fresh idle-state check, records the reviewed source revision and previous native recipe when overwriting, writes the new program, and verifies exact readback. It does not start a brew. Native recipe removal is a distinct idle-only operation.

## Recipes and capabilities

Canonical recipes contain ingredient quantities and units, arbitrary text labels, explicit program steps, water chemistry, fermentation, carbonation, tags and notes. They are versioned independently of a machine. Importing a recipe never determines its physical compartment routing.

Adaptation produces a copy for the selected batch and profile. Grain quantities account for volume and the efficiency ratio; known extract/sugar additions scale by volume. Hop amounts use volume scaling and carry a utilization warning, rather than implying an equivalent bitterness calculation. Model calibration can replace the default batch size, assumed efficiency and grain limit. Firmware capabilities and hard protocol limits cannot be edited through calibration.

The adaptation dialog starts with the effective profile's default batch size,
including saved calibration, rather than copying a potentially oversized source
batch. Missing routing or unsupported native conversions are returned as
`deployment_blockers` alongside a usable planning result. Invalid quantities and
physical capacity violations remain validation errors. Single and bulk writes
require successful compilation; a supplied preview fingerprint also detects
profile changes before a single transfer.

A successful native compilation must satisfy label encoding/length, ID, temperature, step count, timing, routing and payload-size rules. Unknown compartments, incompatible infusions/decoction/timed ramps or unsupported models produce a planning result with deployment disabled. The Z profile describes a single brewing module; it does not pretend to coordinate stock Z2/Z4 hardware.

BeerXML v1 exchange preserves the supported structured fields and a `HUB_STEPS` extension retains an explicit machine program on a hub-to-hub round trip. Arbitrary vendor extensions or all possible BeerXML fields are not guaranteed lossless. JSON is the full-fidelity hub format. See `internal/recipe/README.md` for exact representation and validation rules.

## History

The firmware owns a bounded ring of retained sessions and samples. The hub imports available pages and deduplicates by brew, source and elapsed milliseconds. It keeps archived samples even after they disappear from the device ring. Terminal session fingerprints prevent repeatedly downloading an unchanged finished session. Incomplete or changing pages remain visible as partial data and are retried later. Metadata is merged with compare-and-swap, preserving user notes, ratings and source links edited during network reads.

The source protocol provides elapsed time, not a trustworthy historical wall clock. An observed active session can have an estimated start derived from observation time minus elapsed duration. Previously completed sessions with no observed start keep an unknown start. Gaps and truncation are explicit; a server cannot reconstruct telemetry already overwritten while it was offline. If firmware storage is reset and session IDs are reused, import into a fresh machine registration to keep the archive identities distinct.

Numeric controller/phase values remain numeric unless their meaning is established by the firmware contract. The UI does not invent gravity, pressure, flow or alcohol readings. Simulated traces retain a simulation source label.

## Authentication and persistence

There is one owner account in this release. Passwords use salted PBKDF2-SHA256; session cookies are HttpOnly and SameSite=Strict, and mutation requests require the session's CSRF token and a matching Origin when present. The login throttle keys by client host, not the ephemeral source port. Enable secure cookies behind HTTPS. The application does not trust arbitrary forwarded-client headers.

The data directory has a persistent 32-byte master key. AES-GCM uses each credential's record ID as associated data so ciphertext cannot be silently substituted between a machine and a provider. Config/record changes and encrypted credentials commit together. SQLite uses WAL, full synchronous writes, schema migrations and optimistic record versions. Run one hub process per data directory; this is not a distributed multi-writer service.

## AI

Provider integrations have bounded context and response sizes, request timeouts, refused redirects and no retry loop. API keys stay server-side. Selected recipe and telemetry context is sent only when a configured provider is enabled. A ChatGPT/Claude website subscription is not an API credential.

Authenticated `GET /ai/models` discovers models with a saved provider key. CSRF-protected `POST /ai/models` can test a supplied key without saving it, so initial setup does not require selecting a model before discovery. Both use a separate bounded provider request and return only IDs/display names. Anthropic cursor pages are collected before returning a complete catalog. The browser cancels obsolete lookups on provider/key changes; an older response cannot replace the new provider's list. Provider authentication failures return a provider error, not a Hub session-expiration response.

Recipe creation lives in the recipe workspace. Live advice is scoped to the
machine's verified current brew, and historical analysis is scoped to the
selected journal entry. Archived analysis remains available after its machine
registration is removed. Generated recipes pass canonical validation and still
require review in the editor, an explicit save, adaptation review and deployment.

Automatic monitoring requires consent for one brew. The owner can opt in during
a confirmed brew/manual start or enable it for an already active session. The
start intent is stored only after the machine accepts the command; it binds to
one new matching history session on the same verified device. Pending intent
expires after 15 minutes, is canceled on Hub restart, and cannot attach to a
later brew after an ambiguous session transition. Bound consent uses the device
UID, native history ID and live session ID, and does not roll into the next brew.
The legacy global monitoring setting grants no per-brew consent.

Manual requests and the monitor share a concurrency gate. Automatic attempts
occur at most once every five minutes for the consented brew, with the last
attempt persisted across restart. Each brew retains its latest 200 successful
advice/analysis records and 20 errors; stored advice text is capped at 64 KiB.
Unbound recipe advice retains the latest 500 records. Live and journal queries
filter by their exact context, and brew JSON exports include the saved advice.

Advice cannot call machine operations. The firmware's ordinary controls remain
explicit owner actions. Live recipe/temperature changes are absent from the
current firmware contract; an AI setting cannot add them.

## Deployment boundary

A local Mac, a LAN server, a NAS container and a cloud VM can run the same application. Cloud control requires a private network route to each machine. There is no mandatory vendor service, and the device's own UI and ongoing brew do not depend on the hub. An outbound site agent, public multi-tenant accounts, stock-protocol emulation and provider-controlled live adjustments are outside this implementation.
