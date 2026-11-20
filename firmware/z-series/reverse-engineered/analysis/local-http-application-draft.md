# WIP: incoming HTTP application and ESP32 socket adapter

This is a preservation checkpoint, **not a validated reconstruction**. Its
verified parent is `b2eb13750c159ba7334314732e351153f5956d92`. Existing parser,
connection, state-serialization and Core-event evidence does not cover these
new application or SDK bindings. No new proof, build, application link or
hardware check was run for this WIP commit.

## Preserved draft scope

- `local_http_application.hpp/.cpp` bind the recovered server to live control
  state, the existing recipe parser, `ProgramBuffer` and `LocalRecipePublication`.
  The draft POST route accepts `/recipe`, writes an empty 200 response on
  success and a bounded text 400 response on failure.
- `local_http_platform.hpp/.cpp` bind the transport callbacks to the pinned
  Arduino ESP32 `WiFiServer` and `WiFiClient`, with port 80 and four clients.
- `build_esp32_objects.py` adds the WiFi include directory. The outgoing/WiFi
  workstream has the same change in `1a77bdac9ad22e86d9f3c30eb0d3deaa13ede8ea`;
  retain only one copy when composing branches.

The `program build error` response is a proposed bounded repair for original
assertion behavior. Its semantics and evidence remain to be reviewed. The
SDK constructor, listener status conversion, callback behavior and short-write
handling have not yet been compiled or exercised for this adapter. The build
script discovers these draft source files automatically, so the WIP branch
must not inherit earlier build-success claims.

## Exact next proof and build steps

1. Add a separate incoming-application original-instruction harness based on
   `tests/ghidra/EmulateProgramRecipe.java`. Execute POST dispatch `0x401172b8`,
   recipe parsing/building `0x40116968`, event reset `0x401bf484` and mailbox
   publication `0x400f02e4`. Keep the original Z119 image and existing evidence
   unchanged. Compare status, MIME, body bytes, owned program and parameters,
   freeze state, complete event payload and pending-mailbox behavior. Include
   valid recipes, JSON errors, unknown paths and an already occupied mailbox;
   document bounded-builder failures separately from original equivalence.
2. Add host integration coverage that sends framed requests through
   `LocalHttpServer` and `LocalHttpApplication`, uses the real pinned JSON
   adapter and verifies the resulting program and readiness event. Cover
   fragmented headers/body, `/state` from concrete live state, unknown POST
   routes, malformed recipes and response-write failure. Reuse
   `tests/program_recipe_native.py` dependency verification; do not substitute
   a mock recipe parser or no-op publication callback.
3. Compile the actual application and socket adapter against the historical
   target SDK. Run from `firmware/z-series/reverse-engineered`:

   ```sh
   Z_RECOVERY_TOOLS=/workspace/scratch/66a40079195c/analysis-tools
   python3 tools/build_esp32_objects.py \
     --toolchain "$Z_RECOVERY_TOOLS/esp32-reference/xtensa-esp32-elf" \
     --arduino "$Z_RECOVERY_TOOLS/esp32-reference/arduino-esp32-9a7946e6859130146e56231c27a4aa177d9175a1" \
     --arduinojson "$Z_RECOVERY_TOOLS/arduinojson-reference/ArduinoJson-0b03321ed33fa6965114424fbb023260120c856b" \
     --output /workspace/scratch/a2d75c169cb9/z-http-analysis/application-esp32
   ```

4. Record fresh source-bound comparison and target-build evidence only after
   those checks succeed. Application lifetime/enable wiring and the original
   HTTP console registration (`0x400f7dc8`, commands `i`, `start`, `stop`) still
   need composition with the real application. Target object compilation alone
   does not establish a complete application link or a flashable image.
