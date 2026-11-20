// Reconstructed from PicoBrew PicoStill 0.0.35 lookup tables and bounds.
// Original names/values: analysis/enums.json. This is not the lost vendor source.
// No original image or absolute application RAM is used at runtime.
#include "picostill/enums.hpp"

namespace picostill { namespace oem {

const char* to_string(ProgramId value) noexcept {
    static const char* const names[] = {
        "None",
        "PumpTest",
        "HardwareTest",
        "PinTest",
        "FullTest",
        "Cleaning",
        "PicoEssentialOils",
        "PicoManual",
        "RequestedCleaning",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < ProgramIdCount ? names[index] : "<ProgramId::INVALID>";
}

const char* to_string(StillError value) noexcept {
    static const char* const names[] = {
        "None",
        "UnhandledError",
        "WifiSsidNotFound",
        "WifiInvalidCredentials",
        "WifiConnectingTimeout",
        "WifiNoInternet",
        "FanFailure",
        "SensorOutOfRange",
        "SensorFailure",
        "PressureLeak",
        "NoVacuumSeal",
        "PumpClogged",
        "HardwareNotConnected",
        "SensorConnectionWrong",
        "FanConnectionWrong",
        "PressureLeak_HardwareNotConnected",
        "NoVacuumSeal_HardwareNotConnected",
        "PumpClogged_HardwareNotConnected",
        "PressureLeak_ConnectionWrong",
        "NoVacuumSeal_ConnectionWrong",
        "PumpClogged_ConnectionWrong",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < StillErrorCount ? names[index] : "<StillError::INVALID>";
}

const char* to_string(StillState value) noexcept {
    static const char* const names[] = {
        "Booting",
        "WaitingSsid",
        "Configuring",
        "CheckingUpdate",
        "Updating",
        "WaitingPico",
        "Connecting",
        "Ready",
        "ManualProgramSelect",
        "ProgramRunning",
        "ProgramCompleted",
        "ErrorPending",
        "ErrorSending",
        "Error",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < StillStateCount ? names[index] : "<StillState::INVALID>";
}

const char* to_string(PumpTestState value) noexcept {
    static const char* const names[] = {
        "Starting",
        "CheckingVacuumSeal",
        "CheckingClog",
        "CheckingLeak",
        "Evaluating",
        "Done",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < PumpTestStateCount ? names[index] : "<PumpTestState::INVALID>";
}

const char* to_string(HWState value) noexcept {
    static const char* const names[] = {
        "Off",
        "On",
        "CondenserOutputOverTemp",
        "PumpOff",
        "Cleaning",
        "PumpTest",
        "HardwareTest",
        "PinTest",
        "FullTest",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < HWStateCount ? names[index] : "<HWState::INVALID>";
}

const char* to_string(EventType value) noexcept {
    static const char* const names[] = {
        "Error",
        "AccessPointReady",
        "Connected",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < EventTypeCount ? names[index] : "<EventType::INVALID>";
}

const char* to_string(WifiState value) noexcept {
    static const char* const names[] = {
        "Booting",
        "Idle",
        "Scanning",
        "AccessPoint",
        "AccessPointDisconnecting",
        "PoweringUp",
        "Connecting",
        "Checking",
        "Connected",
        "Disconnecting",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < WifiStateCount ? names[index] : "<WifiState::INVALID>";
}

const char* to_string(Animation value) noexcept {
    static const char* const names[] = {
        "AllOff",
        "AllOn",
        "SteadyGreen",
        "SteadyYellow",
        "SteadyRed",
        "FastGreen",
        "FastYellow",
        "FastRed",
        "SlowGreen",
        "SlowYellow",
        "SlowRed",
        "Busy",
        "BusyRolling",
        "Booting",
        "WaitingSsid",
        "Updating",
        "WaitingPico",
        "Ready",
        "ProgramRunning",
        "UnhandledError",
        "WifiSsidNotFound",
        "WifiInvalidCredentials",
        "WifiConnectingTimeout",
        "WifiNoInternet",
        "FanFailure",
        "SensorOutOfRange",
        "SensorFailure",
        "PressureLeak",
        "NoVacuumSeal",
        "PumpClogged",
        "HardwareNotConnected",
        "SensorConnectionWrong",
        "FanConnectionWrong",
        "PressureLeak_HardwareNotConnected",
        "NoVacuumSeal_HardwareNotConnected",
        "PumpClogged_HardwareNotConnected",
        "PressureLeak_ConnectionWrong",
        "NoVacuumSeal_ConnectionWrong",
        "PumpClogged_ConnectionWrong",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < AnimationCount ? names[index] : "<Animation::INVALID>";
}

}} // namespace picostill::oem
