#!/usr/bin/env python3
"""Record address/byte provenance for manually reconstructed PicoStill functions."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parents[2] / "tools/firmware_analysis"))
from esp_image import mapped_offset, parse_esp32

ENTRIES = {
    0x400d405c: "Diagnostics::check", 0x400d4678: "Diagnostics::require",
    0x400d4074: "thermistor_kelvin", 0x400d46b8: "exponential_moving_average",
    0x4014a084: "SensorMetrics::sampling_threshold_met",
    0x400d4204: "SensorMetrics::read_success_rates_good", 0x400d426c: "SensorMetrics::values_in_range",
    0x400d4154: "FanMetrics::running_fast_enough", 0x400d4180: "fans_have_issue",
    0x4014a0a0: "safe_to_resume_pump", 0x400d554c: "error_animation", 0x400d5734: "latch_first_error",
    0x400d43d0: "Button::set_state", 0x400d4428: "Button::on_edge", 0x400d4408: "Button::emit",
    0x400d446c: "Button::poll",
    0x400d2ee4: "OutputPin::set", 0x400d2f68: "OutputPin::set_pump",
    0x400d2e3c: "Fan::setup", 0x400d2e64: "Fan::set", 0x400d4740: "Fan::sample",
    0x400d2f00: "FanBank::set_power", 0x400d2f34: "FanBank::set_first",
    0x4014a074: "FanBank::second_enabled", 0x400d32a4: "Leds::set_all",
    0x400d32c4: "Leds::toggle", 0x400d32e4: "Leds::setup",
    0x400d45c4: "Leds bounded buffer append", 0x400d5bc4: "Leds::clear_program",
    0x400d5cc4: "Leds string append", 0x400d5ce0: "Leds::start(text, repeat)",
    0x400d5dbc: "Leds::start(Animation), valid enum domain", 0x400d5be0: "Leds::poll",
    0x400d4a68: "PumpTest::set_state", 0x400d4af0: "PumpTest::tick",
    0x400d8688: "board_thermistor: calibration stores within static initializer",
    0x400d2f9c: "AdcReader::clear", 0x400d6ed4: "ADC 12-bit word read",
    0x400d6ef4: "AdcReader::read", 0x400d68c4: "PressureReader::read",
    0x400d2fbc: "Sensors::reset_metrics", 0x400d6f7c: "Sensors::sample_temperatures",
    0x400d6964: "Sensors::sample_pressure", 0x400d7474: "ArduinoPorts::initialize_hardware",
    0x400d2730: "Hardware::capture_into", 0x400d2eb0: "Hardware::enabled",
    0x400d4320: "Hardware::sensors_in_range", 0x400d2fe4: "Hardware::sensors_ready",
    0x40149f34: "Hardware::fans_ready", 0x400d41cc: "Hardware::fans_issue",
    0x400d4334: "Hardware::check_error", 0x400d71e8: "Hardware::poll",
    0x400d2ff8: "Hardware::enable_sensors_internal", 0x400d30e4: "Hardware::enable_sensors",
    0x400d2838: "sensor_connections_match", 0x400d3124: "ConnectionTest::finish",
    0x400d3194: "ConnectionTest::tick", 0x400d3210: "PinTest::tick",
    0x400d6a08: "Controller::set_state", 0x400d6c34: "Controller::start",
    0x400d6cf4: "Controller::stop", 0x400d7288: "Controller::poll",
    0x400d4c98: "Application::queue_button", 0x400d4c30: "Application::queue_wifi",
    0x400d8e64: "Application::set_state", 0x400d903c: "Application::handle_button",
    0x400d9120: "Application::handle_wifi", 0x400d5df0: "Application::select_menu, valid new index domain",
    0x40149fac: "Application::request_program", 0x400d2b18: "Application::request_stop",
    0x400d95cc: "Application::poll and state advancement", 0x400d2a64: "Application::restart",
    0x400d5f38: "WifiStatus::connect",
    0x400d3350: "CredentialStorage::begin", 0x400d4130: "CredentialStorage::initialize",
    0x400d4830: "CredentialStorage::load", 0x400d3534: "CredentialStorage::save",
    0x400d5fa4: "Wifi::set_state", 0x400d63b0: "Wifi::poll",
    0x400d638c: "Wifi::fail", 0x400d27d8: "wifi_status_label",
    0x400d35b0: "parse_http", 0x400d2bf0: "parse_request_line, URI length <= 255",
    0x400d4dc8: "parse_wifi_form, terminated body domain", 0x400d4d20: "url_decode, nonzero capacity domain",
    0x400d2c5c: "string_boolean", 0x400d2b74: "string_equal",
    0x400d2bb8: "string_starts_with", 0x4014a054: "find_character",
    0x40149ff0: "string_equal non-null loop", 0x4014a014: "string_starts_with non-null loop",
    0x400d780c: "HttpServer::poll and dispatch", 0x400d3bf8: "HttpServer::write_page, data",
    0x400d3c80: "HttpServer::write_page, Wi-Fi", 0x400d38f8: "HttpServer::write_browser_state",
    0x400d3b44: "HttpServer::write_scan_options", 0x400d3a18: "HttpServer::write_state",
    0x400d3d1c: "HttpServer::write_factory_result, count <= 200", 0x400d2820: "HTTP end_headers",
    0x400d37d4: "HTTP JSON key", 0x400d37f4: "HTTP boolean_field",
    0x400d3820: "HTTP decimal_field", 0x400d3858: "HTTP string field",
    0x400d387c: "HTTP quoted JavaScript field", 0x400d38b4: "HTTP unquoted JavaScript field",
    0x400d38dc: "HTTP boolean JavaScript field",
    0x400d4860: "bounded_copy, nonzero capacity and non-null pointer domain",
    0x400d48f4: "hex_digit", 0x400d4928: "encode_hex, sufficient capacity domain",
    0x400d4984: "DeviceData::update_token", 0x400d49e0: "DeviceData::set_ssid", 0x400d49f8: "DeviceData::set_password",
    0x400d4a10: "ApplicationDevice::configure", 0x400d48b0: "DeviceData::set_uid, literal MAC domain", 0x400d50e8: "DeviceData::initialize",
    0x400d5938: "ApplicationReports::controller_changed", 0x400d58c4: "128-byte report/console buffer clear",
    0x400d58fc: "report/console string append", 0x400d44f8: "report/console bounded append", 0x400d58e4: "report/console append guard",
    0x400d2c90: "ApplicationReports::snapshot_report", 0x400d2d88: "ApplicationReports::full_test_report",
    0x400d44cc: "Backend::reset", 0x400d8834: "Backend::sync", 0x400d8864: "Backend::request_firmware",
    0x400d8770: "Backend::send", 0x400d76e0: "Backend::poll", 0x400d4534: "Backend::complete",
    0x400d5b5c: "backend request prefix", 0x400d5b00: "backend request bounded append", 0x400d5b40: "backend request string append", 0x400d5f88: "Backend::fail",
    0x400d5e1c: "UpdateParameters::parse, bounded URL domain", 0x400d37a4: "UpdateParameters::report",
    0x400d8880: "FirmwareUpdate::begin", 0x400d91a8: "FirmwareUpdate::poll", 0x400d28c8: "updater header value substring",
    0x400d7510: "Machine::setup", 0x400d8a00: "Machine::dispatch_console", 0x400d764c: "Machine::receive_console", 0x400d5918: "Machine::queue_console",
    0x400d3320: "Machine::report_eeprom", 0x400d3f48: "Machine::report_info", 0x400d897c: "Machine::start_firmware_update",
    0x400d3eec: "Machine::sleep", 0x400d3f0c: "Machine::factory_reset", 0x400d5d40: "Machine::test_leds",
    0x400d7628: "ESP32 setup", 0x400d97dc: "ESP32 loop",
    0x400d67bc: "Machine::self_tests runner", 0x400d65f8: "embedded buffer self-test vectors",
    0x400d4e8c: "embedded string self-test vectors", 0x400d51c0: "embedded identity self-test vectors",
    0x400d5414: "embedded request parser self-test vectors", 0x400d5ea4: "embedded updater URL self-test vectors",
    0x400d5504: "embedded moving average self-test vector", 0x400d5f68: "embedded pressure point self-test output",
    0x400d55d8: "embedded NVS save/load/restore self-test", 0x400d57a8: "embedded sensor threshold self-test vectors",
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", type=Path, required=True)
    args = parser.parse_args()
    raw = (ROOT.parent / "oem/upstream/picostill_0_0_35.bin").read_bytes()
    image = parse_esp32(raw)
    manifest = json.loads((args.export / "manifest.json").read_text())
    if manifest["input"]["sha256"] != image["sha256"]:
        raise ValueError("Wrong decompiler input")
    functions = {int(f["address"], 16): f for f in json.loads((args.export / "functions.json").read_text())}
    records = []
    for address, name in sorted(ENTRIES.items()):
        function = functions[address]
        ranges = []
        for region in function["ranges"]:
            start, end = int(region["start"], 16), int(region["end"], 16)+1
            offset = mapped_offset(image, start, end-start)
            ranges.append({"start": start, "size_bytes": end-start,
                           "sha256": hashlib.sha256(raw[offset:offset+end-start]).hexdigest()})
        validation="original instruction comparisons"
        if address==0x400d7474: validation="reviewed original setup order and target compilation; no setup replay or physical test"
        if address==0x400d8688: validation="original calibration-store execution; unrelated IP/application constructors are explicit external boundaries"
        if address in {0x400d2d88,0x400d8864,0x400d3eec,0x400d3f0c,0x400d5d40,0x400d7628,0x400d97dc}:
            validation="reviewed original instructions, concrete application wiring and complete SDK link; no direct original comparison for this wrapper"
        if address in {0x400d67bc,0x400d65f8,0x400d4e8c,0x400d51c0,0x400d5414,0x400d5ea4,0x400d5504,0x400d5f68,0x400d55d8,0x400d57a8}:
            validation="recovered embedded vectors execute owned code in native integration test; no differential execution of the original self-test runner"
        if address==0x400d7510: validation="original application boot ordering and data/LED/credential effects compared; hardware setup, NVS wrapper and OS calls are explicit boundaries"
        if address==0x400d897c: validation="original no-address rejection compared; valid-address wrapper reviewed and linked to independently compared updater implementation"
        records.append({"name": name, "entry": address, "ranges": ranges, "validation":validation,
                        "original_instruction_count": function["instruction_count"]})
    files = ["src/core.cpp", "include/picostill/core.hpp", "tools/ghidra/PicoStillExecution.java",
             "tools/ghidra/EmulatePicoStillCore.java", "tools/core_cases.py", "tests/fixtures/core-inputs.json",
             "tests/fixtures/core-original.json", "src/hardware.cpp", "src/animations.cpp",
             "include/picostill/hardware.hpp", "tools/hardware_cases.py", "tools/recover_animations.py",
             "tools/ghidra/EmulatePicoStillHardware.java", "tests/fixtures/hardware-inputs.json.gz",
             "tests/fixtures/hardware-original.json.gz"]
    files += ["src/sensors.cpp", "include/picostill/sensors.hpp", "tools/sensor_cases.py",
              "tools/ghidra/EmulatePicoStillSensors.java", "tests/fixtures/sensors-inputs.json.gz",
              "tests/fixtures/sensors-original.json.gz", "platform/esp32/arduino_ports.hpp",
              "platform/esp32/arduino_ports.cpp"]
    files += ["src/controller.cpp", "include/picostill/controller.hpp", "tools/controller_cases.py",
              "tools/ghidra/EmulatePicoStillController.java", "tests/fixtures/controller-inputs.json.gz",
              "tests/fixtures/controller-original.json.gz"]
    files += ["src/application.cpp", "include/picostill/application.hpp", "tools/application_cases.py",
              "tools/ghidra/EmulatePicoStillApplication.java", "tests/fixtures/application-inputs.json.gz",
              "tests/fixtures/application-original.json.gz", "tests/application_bridge.cpp",
              "tests/controller_bridge.cpp", "tests/test_application.py"]
    files += ["src/storage.cpp", "include/picostill/storage.hpp", "platform/esp32/nvs_ports.cpp",
              "platform/esp32/nvs_ports.hpp", "tools/storage_cases.py", "tools/ghidra/EmulatePicoStillStorage.java",
              "tests/fixtures/storage-inputs.json.gz", "tests/fixtures/storage-original.json.gz",
              "tests/storage_bridge.cpp", "tests/test_storage.py"]
    files += ["src/wifi.cpp", "include/picostill/wifi.hpp", "platform/esp32/wifi_ports.cpp",
              "platform/esp32/wifi_ports.hpp", "tools/wifi_cases.py", "tools/ghidra/EmulatePicoStillWifi.java",
              "tests/fixtures/wifi-inputs.json.gz", "tests/fixtures/wifi-original.json.gz",
              "tests/wifi_bridge.cpp", "tests/test_wifi.py"]
    files += ["src/http.cpp", "include/picostill/http.hpp", "platform/esp32/http_ports.cpp",
              "platform/esp32/http_ports.hpp", "tools/http_cases.py", "tools/ghidra/EmulatePicoStillHttp.java",
              "tests/fixtures/http-inputs.json.gz", "tests/fixtures/http-original.json.gz",
              "tests/http_bridge.cpp", "tests/test_http.py"]
    files += ["src/http_server.cpp", "include/picostill/http_server.hpp", "src/web_data.cpp", "src/web_wifi.cpp",
              "include/picostill/web_assets.hpp", "tools/recover_web.py", "tools/http_server_cases.py",
              "tools/ghidra/EmulatePicoStillHttpServer.java", "tests/fixtures/http-server-inputs.json.gz",
              "tests/fixtures/http-server-original.json.gz", "tests/http_server_bridge.cpp", "tests/test_http_server.py"]
    for stem in ["data","backend","update","machine"]:
        files += ["tools/"+stem+"_cases.py", "tests/"+stem+"_bridge.cpp", "tests/test_"+stem+".py",
                  "tests/fixtures/"+stem+"-inputs.json.gz", "tests/fixtures/"+stem+"-original.json.gz"]
    files += ["tools/ghidra/EmulatePicoStillData.java", "tools/ghidra/EmulatePicoStillBackend.java",
              "tools/ghidra/EmulatePicoStillUpdate.java", "tools/ghidra/EmulatePicoStillMachine.java"]
    for stem in ["data","device","reports","backend","update","machine"]:
        files += ["src/"+stem+".cpp", "include/picostill/"+stem+".hpp"]
    files += ["src/self_tests.cpp", "include/picostill/transport.hpp", "platform/esp32/application_main.cpp",
              "platform/esp32/outbound_ports.hpp", "platform/esp32/outbound_ports.cpp",
              "platform/esp32/system_ports.hpp", "platform/esp32/system_ports.cpp",
              "tools/build_esp32_objects.py", "tools/link_esp32_application.py", "analysis/esp32-application-link.json"]
    report = {"schema_version": 1, "input_sha256": image["sha256"], "functions": records,
              "files": {name: digest(ROOT / name) for name in files},
              "scope": "Recovered complete PicoStill application composition: hardware/controller, application states, NVS credentials, identity, Wi-Fi, HTTP routes/browser, backend requests, updater, console and boot. Original instruction comparisons cover the listed entries with explicit boundaries; wrapper/self-test limits are recorded per entry. Concrete ESP32 adapters and original SDK main link as a complete ELF. No physical installation, board or flash-layout qualification.",
              "application_reconstruction_implemented": True, "reconstruction_complete": False,
              "complete_application_linked": True, "hardware_tested": False,
              "outstanding": ["Modified authenticated v1 API, standalone browser and Hub integration",
                              "Direct original comparison of destructive system wrappers and embedded self-test runner",
                              "Physical flashing/boot/actuator and recovery qualification"]}
    (ROOT / "analysis/source-evidence.json").write_text(json.dumps(report, indent=2)+"\n")
    print("Recorded %d reviewed OEM instruction entries" % len(records))


if __name__ == "__main__":
    main()
