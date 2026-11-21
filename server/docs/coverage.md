# Feature and hardware coverage

This table records implemented behavior. It is deliberately not a claim of feature parity with every release of every PicoBrew project.

The [project target](../../docs/project-roadmap.md) is custom firmware for
Pico C, S/Pro, Z-family and Zymatic with a common standalone API and local browser
experience. The table below describes the current implementation, not the final
model scope.

| Area | Implemented | Boundary |
|---|---|---|
| Fleet | Persistent machine registry, simultaneous independent polling, connection status, live browser stream | One owner; no team roles or remote-site agent |
| Pico S/Pro | Adapter for the standalone API v1: OLED, dial, programs, stop latch, recipes, preferences, history and forwarding | Requires that firmware; protocol-tested with local HTTP fixtures, not newly tested on a physical brewer in this project |
| Pico C | Standalone C API adapter, automatic identity/telemetry pairing, explicit OLED geometry, recipes/history and controller-scoped packages | Requires the reconstructed C standalone firmware; native/host tested, physical installation and brewing unverified; no OTA installer |
| Generic Z-series | Standalone API adapter, four-page grayscale display, native commands, canonical local recipes with direct valves 1–6, named sensor history | Generic identity does not establish a Z1/Z2 mapping; archived Z/Zymatic program grammar remains blocked pending explicit conversion; physical validation unperformed |
| PicoStill | Capability-driven native mode, pump and fan controls; named temperature/pressure readings and history | Requires reconstructed standalone firmware; no invented heater, recipe catalog or front panel; physical validation unperformed |
| PicoFerm | Fermentation sessions, samples, sampling interval, confirmed calibration, temperature/pressure/battery readings and history | Native pressure units remain unestablished and are labeled raw; no recipes, heater or front panel; physical validation unperformed |
| Zymatic | Recipe planning, quantity adaptation and export; simulated operation for demos | No live standalone adapter is claimed for stock Zymatic firmware |
| Recipe library | CRUD, revision conflict protection, archived revisions, ingredient plans and distinct native-program editor | Program-only records have no inferred ingredients, batch volume or efficiency; full ingredient/style databases are not included |
| Community catalog | Recipes → Browse all: 5,216 curated entries, rounded display quantities, expandable sources/credits, 88 bundled image/text assets, rendered guides and independent library copies | Original measurements and records are preserved; optional PDF manuals require local installation |
| Brewing details | Water chemistry, fermentation/carbonation metadata, mash and boil attributes | Metadata does not imply a connected fermenter or automatic addition hardware |
| Adaptation | Profile-sized defaults, volume/efficiency scaling, saved calibration, planning previews with deployment blockers, native compilation and stale-preview checks | Efficiency defaults are estimates; S/Pro grain capacity is unknown. Hop utilization is not fully modeled; plumbing must be explicitly supplied |
| Recipe deployment | Single and bulk transfer; capacity preview, explicit matching-ID updates, profile/revision/inventory checks, native write/readback, durable receipts and interruption recovery | Does not start automatically; an uncertain write stops the batch and is never automatically repeated or rolled back |
| Installed recipes | Read, edit with expected-value check, deploy/overwrite, choose for brewing, remove while idle | Limited by the appliance's slots and protocol; the firmware has no atomic conditional-write operation across independent browser clients |
| History | Retained-session imports, durable samples, named sensor/actuator channels and units, temperature/state graphs, notes/rating, CSV/JSON | Cannot recover overwritten source samples; old start times may be unknown |
| AI recipe creation | Brew assistant in the recipe editor, OpenAI/Anthropic API integration, validated structured drafts, review in recipe editor | Provider key/model and external API access required |
| AI model discovery | Account model catalog, automatic lookup with a saved key, pre-save connection, refresh and manual entry | Model-list permission and chat compatibility are distinct; discovery does not invoke inference |
| AI monitoring | Opt-in at start or during a live brew; exact-session consent, contextual questions, five-minute observations saved with that brew | Advisory; no low-level control or live recipe/temperature overrides; consent does not carry into the next brew |
| AI analysis | Analysis in the selected journal entry, saved results and JSON export, downsampled bounded context | No invented measurements; raw archive/export remains available; a removed machine does not remove archived analysis |
| Shopping list | Aggregate selected recipes by ingredient and unit | Inventory purchasing, supplier integrations and stock deduction are absent |
| Machine identity | Read C/S/Pro, generic Z-series and accessory identities after authenticated UID verification; explicit fallback for older S/Pro firmware | Reported identity describes the installed build, not independent hardware detection; C package applicability also requires a separate UID-bound controller observation |
| Telemetry forwarding | Automatic Hub connection on pairing; persistent advanced manual override in the Hub; ingest known-UID unsigned observations | Unsigned forwarding is not authoritative status or a control channel |
| Data | SQLite persistence, encrypted credentials, portable nonsecret ZIP, full-directory recovery instructions | No browser upload/restore of a full encrypted data directory |
| API | Authenticated REST endpoints, OpenAPI schemas/catalog, in-app explorer, SSE | Hub authentication differs from the appliance's bearer-key API |
| Packaging | Source, pinned dependencies, Dockerfile, Compose, Go/frontend/browser CI | No managed hosting or app-store package |

## Prior projects considered

The product scope was informed by these primary project descriptions and documentation:

- [chiefwigms/picobrew_pico](https://github.com/chiefwigms/picobrew_pico): local cloud replacement, appliance-specific protocol handling and session/recipe workflows.
- [hotzenklotz/picobrew-server](https://github.com/hotzenklotz/picobrew-server): Zymatic protocol and local recipe/session server.
- [PicoClaw](https://github.com/cetezadi/picobrew_picoclaw): another community PicoBrew server implementation.
- [Small Brew Companion documentation](https://www.smallbrewcompanion.com/docs/): recipe creation, equipment/brewing profiles, brewing logs, ingredients/water/fermentation/carbonation and shopping-list workflows.
- [Standalone Pico S/Pro API contract](../../firmware/pico-s-pro/modified/docs/standalone-api.md) and [OpenAPI](../../firmware/pico-s-pro/modified/docs/openapi.json): the implemented hardware contract in this checkout and the reference for subsequent firmware ports.

No application code from those servers or from GoMVC is imported. The UI and
server are an independent implementation. New machine support should come from
firmware ports that implement the shared contract, with the Hub respecting each
machine's verified capabilities and limits. A planning profile alone does not
establish live hardware support.
