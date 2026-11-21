# Verification record

## Interrupted-brew recovery — local checks, 20 November 2026

- Authenticated device/server Go tests passed, including bounded optional recovery
  metadata, preparation checkpoints, explicit resume confirmation and a discard
  request with no extra device arguments.
- All 129 frontend tests in 19 suites passed. TypeScript and the production build
  passed. The deterministic bundle contains 228 files, 2,696,433 ZIP bytes and
  SHA-256 `1cb5688235c6f0a3544f3daa3fbacf65c277f028ec6216323b38b09643103252`.
- Six Chromium family cases passed at 1280/390-pixel widths. The Z cases exercise
  preparation recovery, cancellation without a command, confirmed resume, invalid
  checkpoint discard when `available:false`, inhibited new programs and mobile
  layout. Accessory controls and history remain covered by the same suite.
- The subsequent cadence integration passed the affected Go device/server suites
  and TypeScript/production bundle checks. It preserves per-session sampling
  intervals in imports and JSON exports, rejects invalid bounds and invalidates
  a cached prefix when that metadata changes. The shared device browser separately
  passed all six real-browser cases including an older-session JSON download
  whose cadence differs from the newest inventory entry.

The Hub uses its embedded website and ordinary authenticated/idempotent command
path. These local fixtures do not start or contact a physical brewer and do not
establish physical recovery behavior or a remote CI result.

## Generic Z and accessory integration — local checks, 20 November 2026

- Go 1.26.0: all package tests passed. The affected device, server and recipe
  packages also passed the race detector.
- Frontend: all 129 tests in 19 suites passed; TypeScript and the production
  bundle build passed. The deterministic bundle checker verified 228 files,
  2,695,907 ZIP bytes and SHA-256
  `1957b1433fc280c7fd5ee968b726958ff67648ccb8763d0c38adcd8309532dd5`.
- Chrome for Testing Headless Shell 158.0.8127.36: twelve browser cases passed.
  Six exercise PicoFerm, PicoStill and generic Z at 1280/390-pixel widths, two
  preserve machine identity/receiver behavior, and four preserve both Pico C
  display variants and package workflows at desktop/mobile widths.
- The family cases exercise calibration cancellation and confirmation, explicit
  false pump settings, fan channel/level, sampling interval, grayscale pixels,
  advertised Z commands, per-session history fields, unit labels and the absence
  of recipe/front-panel requests for accessories.
- The Go-only startup fixture built an isolated server without firmware or
  frontend source and without Node/npm on PATH. Initial startup and restart
  served the embedded website, drained an accepted write on terminal interrupt,
  closed the listener/stream and reopened the same persisted recipe.

These are local protocol, browser and executable checks with fixtures. No
appliance was contacted or flashed. They do not establish physical qualification
or a successful remote CI run. See [family controls](family-devices.md).

## Independent server layout — local checks

The Hub lives in `jeffreydwalter/picobrew-pico-pro-firmware/server` and uses
that Go module path. The directory move and per-machine firmware layout passed:

- Full Go package tests with the race detector, `go vet ./...`, and a production
  build using Go 1.27.4 on Linux.
- All eight Python tests, with `CATALOG_REQUIRE_FIRMWARE=1` checking the starter
  data in `firmware/pico-s-pro/modified/data/starter-recipes.json`.
- Frontend typecheck and production build, all 22 unit tests, and all seven DOM
  integration tests against its isolated Go simulator.
- A separate copy of `server/` with no sibling firmware tree: production Go
  build, simulator startup, health response and the static website all passed.
  Its Python suite passed with the one optional firmware comparison skipped;
  setting `CATALOG_REQUIRE_FIRMWARE=1` correctly failed for missing firmware data.
- Catalog preservation: the directory move retains the exact 18,546,799-byte
  archive, SHA-256
  `81b8923c7402031862521f0ae33ff4e13c972728a6592ad68d4ab938c13c5f60`.

During the preceding repository migration, a complete catalog regeneration
from the four pinned recipe/support source checkouts and the five checked-in
icon/notice files produced that same archive byte-for-byte.

These checks used no historical Zymatic server checkout or implementation.
Chromium and Docker results for the migrated revision must come from its own
GitHub Actions run; the historical runs below do not establish those results.

## Community catalog and bulk transfers — local checks, 19 November 2026

- Full Go package suite with the race detector, `go vet ./...`, and production build: passed.
- Eight Python conversion, static-asset and cross-component tests: passed. The mandatory starter check compares the firmware's eight default programs with the bundled catalog, including IDs, names, exact steps and source hashes.
- Catalog integrity checks cover every copyable entry, every advertised native deployment, original source checksums, excluded malformed/empty records, local asset containment and immutable source data.
- Transfer tests cover slot limits, preserving existing programs by default, explicitly updating matching IDs, no duplicate execution, changes to source revisions/profiles/inventory/capabilities, busy machines, uncertain writes and receipt recovery after restart.
- Installed-slot editing tests cover idle-state checks, stale expected values, verified write outcomes and absence of automatic brew starts.
- Frontend typecheck/build and all 22 unit tests: passed.
- All seven DOM integration tests against an isolated Go simulator: passed. The added workflows exercise catalog lookup, copying, exact zero-minute schedules, bulk preview/transfer, installed-slot editing, overwrite prevention, cleaning records and local artwork/source credits.
- Local Chromium launch was blocked before reaching the application by a denied browser socket operation. Real-browser/mobile assertions and screenshots must be verified in the published commit's GitHub Actions run.

These are local implementation checks. They do not claim a new physical brewing test or a successful remote CI run; the exact published commit's checks provide that evidence.

## Machine management and device-key advisory — local checks

- Full Go race tests, vet and production build: passed.
- Frontend typecheck, production build and all 22 unit tests: passed.
- All 5 DOM integration tests against an isolated Go simulator: passed, including Overview add/edit, offline management, removal cancellation, and retained brew samples after removing a registration.
- Device-key tests cover reuse warnings across active and disabled registrations, exact stored-key preservation, simulator exclusion, changing/removing a duplicate, restart, secret read failures and concurrent fleet snapshots.
- The browser suite now contains 6 tests, including mobile card/modal management. Its result must be taken from the published commit's CI, not inferred from these local checks.

## Model discovery and Settings update — local checks

- All Go package tests with the race detector, `go vet ./...` and a production Go build: passed.
- Frontend typecheck and production build: passed.
- Frontend unit tests: 22 passed, including automatic model loading, connecting an unsaved key, provider changes, stale-response cancellation, manual entry and refresh.
- DOM interaction tests against an isolated real Go server: 4 passed.
- Provider model-list protocol fixtures cover OpenAI credentials, Anthropic pagination, bounded responses, cancellation, redirects and redacted errors. Server tests cover authentication, CSRF, provider-key isolation and discovery without changing saved credentials or configuration.
- Profile regression tests preserve older saved calibration values and existing recipes while applying sourced defaults only to unconfigured profiles or an explicit reset.
- The duplicate Settings API section is removed; browser tests navigate to the explorer from the sidebar.

The later [branch run at b8e2542](https://github.com/jeffreydwalter/picobrew-server/actions/runs/44440433270)
and [PR run](https://github.com/jeffreydwalter/picobrew-server/actions/runs/44440440070)
both passed all four Hub jobs: Linux/macOS Go checks, frontend build with 22 unit,
4 DOM and 5 browser tests, and container build/startup/restart. The simulator build
now has its own deadline before DOM server readiness is timed; a deliberately
31-second delayed build also passed all four DOM tests locally.

## Local checks — 18 November 2026

- Go package tests with the race detector: passed.
- `go vet ./...`: passed.
- React/TypeScript typecheck and production build: passed.
- Frontend unit tests: 15 passed.
- Production dependency audit (`npm audit --omit=dev`): no reported vulnerabilities at the time of the check.
- DOM interaction tests against an isolated real Go HTTP server: 4 passed.
- Local Chromium launch: blocked before any page loads because this workspace denies Chromium's local sockets. The explicit escalation request was rejected by workspace approval policy. This is not recorded as a browser pass.

The checked-in GitHub Actions workflow runs Go tests/builds on Linux and macOS, frontend unit and real-server DOM tests, Chromium integration with desktop/mobile screenshots, and a Docker build/startup/restart check. Consult the PR's latest commit checks for actual CI results; the presence of the workflow alone is not a successful run.

## Covered behavior

- SQLite restart persistence, transactional schema setup, optimistic concurrent saves, atomic encrypted credential/config updates and telemetry deduplication.
- Owner setup/login/logout, session cookies, CSRF/Origin checks, host-based throttling and encrypted key redaction.
- Two independent machines receiving commands concurrently, stable command receipts, repeated-key payload conflicts and unknown delivery without automatic retry.
- Physical REST protocol fixtures for exact payloads, authentication, Content-Length, serialized requests, bounded reads, redirects, cancellation, no mutation retries, recipe readback and history framing.
- Simulator isolation, preparation prompts, stop latch, capacity limits and history lifecycle.
- Recipe units, adaptation/efficiency scaling, hard native bounds, unknown routing, recipe revision conflicts, BeerXML/JSON exchange and deployment without starting.
- History pagination, retained samples after interruptions, concurrent edits preserved across imports, partial data, terminal-session caching and simulation labeling.
- Provider request/authentication contracts, no redirect credential forwarding, bounded errors/context, opt-in monitor cadence, saved advice and invalid AI drafts kept out of the recipe library.
- Frontend framebuffer orientation, chart/sample calculations, request transport, binary/text/SSE explorer handling, forms, recipe/history persistence and equipment calibration.

## Not physically verified by this build

No real PicoBrew was contacted, no firmware was flashed, and no actual heating/pumping command was issued during this implementation. The standalone firmware API is tested using contract fixtures; the next hardware check should begin with connecting the already-installed S/Pro firmware and inspecting discovery/status/OLED/history before operating a prepared machine.

No live paid AI API request was made. Provider request formats and errors were exercised with local HTTP test servers. Model availability and account billing depend on the user's configured provider.

Z and Zymatic planning profiles and simulators are not evidence of stock-hardware protocol compatibility. Pico C now has native reconstructed standalone builds and focused API/Hub tests, but physical installation and brewing remain unverified. The feature coverage document states this boundary explicitly.
