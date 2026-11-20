// Fixed /state document recovered from Z119 HttpServer.h and JSON output bodies.
#pragma once
#include "zseries/local_http_server.hpp"
#include "zseries/heater.hpp"
#include "zseries/program_runtime.hpp"

namespace zseries { namespace oem {
struct LocalHttpState {
    double thermoblock_temperature = 0, wort_temperature = 0;
    double ambient_temperature = 0, drain_temperature = 0;
    std::uint8_t valve_position = 0, pumps = 0;
    std::uint32_t pause_reason = 0;
    std::int32_t rssi = 0;
    double target_temperature = 0;
    const char* step_name = "";
    std::uint32_t elapsed_seconds = 0, remaining_seconds = 0, runtime_flags = 0;
};

// The original document always has fifteen fields and fits its 1024-byte JSON
// pool. This fixed-schema writer preserves original strings/numbers directly,
// including nine-digit double formatting, without allocating a JSON tree.
// Like original 0x40119998, it writes at most capacity-1 bytes and a final NUL.
// A pause reason outside 0..17 or an invalid output range is rejected.
bool serialize_local_http_state(const LocalHttpState&, char* output,
                               std::size_t capacity, std::size_t& written) noexcept;
LocalHttpResult send_local_http_state(LocalHttpServer&, const LocalHttpState&);
// Concrete snapshot of the existing recovered control/runtime types. RSSI is
// the signed original Wi-Fi observation, supplied by the Wi-Fi state owner.
LocalHttpState local_http_state(const HeaterSensors&, const HeaterController&,
                               const z_msp_state&, const ProgramRuntime&, std::int32_t rssi) noexcept;
}} // namespace zseries::oem
