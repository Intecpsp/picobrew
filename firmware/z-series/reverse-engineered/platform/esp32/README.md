# ESP32 target integration

These adapters call the actual Arduino ESP32 1.0.1 / bundled ESP-IDF APIs
identified from the preserved Z119 image. Pin, bus, timer and task assignments
come from the original Core/System constructors and setup, documented in
[`../../analysis/core-startup.md`](../../analysis/core-startup.md).

Compile owned target objects with `tools/build_esp32_objects.py`. This checks
the SDK identity, the selected ArduinoJson dependency, and C++ interfaces and
creates a static archive including the concrete JSON adapter. It does not
link a full application, install an entry point, select partitions or flash a
machine. Full Core, UI, network and persistence assembly remains in progress.

```sh
python3 tools/acquire_esp32.py --destination build/dependencies
python3 tools/acquire_arduinojson.py --destination build/dependencies
python3 tools/build_esp32_objects.py \
  --toolchain build/dependencies/xtensa-esp32-elf \
  --arduino build/dependencies/arduino-esp32-9a7946e6859130146e56231c27a4aa177d9175a1 \
  --arduinojson build/dependencies/ArduinoJson-0b03321ed33fa6965114424fbb023260120c856b
```

ArduinoJson 6.21.6 is an explicitly selected rebuild dependency; the original
firmware's exact minor version remains unknown. Its source is verified against
[`../../dependencies/arduinojson.json`](../../dependencies/arduinojson.json).

`setup_system()` with `system_setup_services()` performs original partition
lookup, SPI/I²C initialization, bus scans, watchdog registration and console
registration. It uses the shared returning diagnostic handler, including its
counter update and 200 ms delay, and continues console setup after watchdog
errors. Its caller-owned bindings require the real console registry; see
[System setup evidence](../../analysis/system-setup.md).
`gpio`, `i2c`, `acquisition_clock` and
`msp_transport` bind portable recovered modules to the SDK. `initialize_msp`
creates the client using the original bus/address/delays but does not issue
controller commands or perform an update on its own.

The timer helpers require all supplied `TimerCallbacks` to remain valid for
the life of the timers. Application assembly must bind its real interrupt
callbacks and account for the original cross-task/interrupt state accesses.
No host fixture or successful object compilation establishes that behavior.

`wifi_platform.*` binds the recovered Wi-Fi manager to the same historical
Arduino radio/DNS methods. `wifi_services()` requires a caller-owned
`WifiBindings` object; its Core event sink and console registry are explicit
application dependencies. Neither binding is replaced with a successful no-op.
See [Wi-Fi reconstruction and validation](../../analysis/wifi.md). The OEM
DNS/access-point behavior is retained here and does not constitute the future
standalone network implementation.

`http_client_platform.*` binds outgoing HTTP socket operations through the
historical Arduino `Client` interface, with actual heap, errno and clock
operations. A caller can supply a WiFiClient or WiFiClientSecure instance and
its explicit class. Automatic client acquisition and the custom Ethernet
client's TLS preparation remain required application callbacks. The recovered
StillClient uses this HTTP engine through `still_http_services()` and
`tick_still_http()`. See [outgoing HTTP behavior and validation](../../analysis/http-client.md).

`buzzer_platform.*` binds the recovered Buzzer and System alarm operations to
historical Arduino LEDC, clock and decimal conversion calls. The console, returning assertion and
diagnostic sinks remain explicit required application bindings. The existing
Core software timer calls `Buzzer::tick()`; application assembly must serialize
its accesses with Core/console mutations and retain the bound objects for the
timer's lifetime. See [buzzer and alarm evidence](../../analysis/buzzer.md).
