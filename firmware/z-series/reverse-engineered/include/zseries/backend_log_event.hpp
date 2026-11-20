// Original ZBackend log-event encoders (0x4011da68 / 0x4011d974).
#pragma once
#include "zseries/console.hpp"
#include "zseries/safeties.hpp"
#include <array>
namespace zseries { namespace oem {
enum class BackendLogEventType : std::uint32_t { None, MspError, SafetyError };
struct BackendLogEvent {
    BackendLogEventType type=BackendLogEventType::None;
    std::array<char,512> json{};
};
ConsoleResult backend_log_msp_error(BackendLogEvent&,std::uint32_t,const ConsoleServices&);
ConsoleResult backend_log_safety_error(BackendLogEvent&,const SafetyState&,const ConsoleServices&);
}} // namespace zseries::oem
