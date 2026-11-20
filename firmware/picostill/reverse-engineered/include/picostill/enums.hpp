// Reconstructed from PicoBrew PicoStill 0.0.35 lookup tables and bounds.
// Original names/values: analysis/enums.json. This is not the lost vendor source.
// No original image or absolute application RAM is used at runtime.
#pragma once
#include <cstddef>
#include <cstdint>

namespace picostill { namespace oem {

// DROM 0x3f412c54; observed bound in 0x400d26fc.
enum class ProgramId : std::uint32_t {
    None = 0,
    PumpTest = 1,
    HardwareTest = 2,
    PinTest = 3,
    FullTest = 4,
    Cleaning = 5,
    PicoEssentialOils = 6,
    PicoManual = 7,
    RequestedCleaning = 8,
};
constexpr std::size_t ProgramIdCount = 9;
const char* to_string(ProgramId value) noexcept;

// DROM 0x3f412c00; observed bound in 0x400d2714.
enum class StillError : std::uint32_t {
    None = 0,
    UnhandledError = 1,
    WifiSsidNotFound = 2,
    WifiInvalidCredentials = 3,
    WifiConnectingTimeout = 4,
    WifiNoInternet = 5,
    FanFailure = 6,
    SensorOutOfRange = 7,
    SensorFailure = 8,
    PressureLeak = 9,
    NoVacuumSeal = 10,
    PumpClogged = 11,
    HardwareNotConnected = 12,
    SensorConnectionWrong = 13,
    FanConnectionWrong = 14,
    PressureLeak_HardwareNotConnected = 15,
    NoVacuumSeal_HardwareNotConnected = 16,
    PumpClogged_HardwareNotConnected = 17,
    PressureLeak_ConnectionWrong = 18,
    NoVacuumSeal_ConnectionWrong = 19,
    PumpClogged_ConnectionWrong = 20,
};
constexpr std::size_t StillErrorCount = 21;
const char* to_string(StillError value) noexcept;

// DROM 0x3f412bbc; observed bound in 0x400d3f48.
enum class StillState : std::uint32_t {
    Booting = 0,
    WaitingSsid = 1,
    Configuring = 2,
    CheckingUpdate = 3,
    Updating = 4,
    WaitingPico = 5,
    Connecting = 6,
    Ready = 7,
    ManualProgramSelect = 8,
    ProgramRunning = 9,
    ProgramCompleted = 10,
    ErrorPending = 11,
    ErrorSending = 12,
    Error = 13,
};
constexpr std::size_t StillStateCount = 14;
const char* to_string(StillState value) noexcept;

// DROM 0x3f412ba4; observed bound in 0x400d4a68.
enum class PumpTestState : std::uint32_t {
    Starting = 0,
    CheckingVacuumSeal = 1,
    CheckingClog = 2,
    CheckingLeak = 3,
    Evaluating = 4,
    Done = 5,
};
constexpr std::size_t PumpTestStateCount = 6;
const char* to_string(PumpTestState value) noexcept;

// DROM 0x3f412c78; observed bound in 0x400d6a08.
enum class HWState : std::uint32_t {
    Off = 0,
    On = 1,
    CondenserOutputOverTemp = 2,
    PumpOff = 3,
    Cleaning = 4,
    PumpTest = 5,
    HardwareTest = 6,
    PinTest = 7,
    FullTest = 8,
};
constexpr std::size_t HWStateCount = 9;
const char* to_string(HWState value) noexcept;

// DROM 0x3f412bf4; observed bound in 0x400d4c30.
enum class EventType : std::uint32_t {
    Error = 0,
    AccessPointReady = 1,
    Connected = 2,
};
constexpr std::size_t EventTypeCount = 3;
const char* to_string(EventType value) noexcept;

// DROM 0x3f4129a8; observed bound in 0x400d5fa4.
enum class WifiState : std::uint32_t {
    Booting = 0,
    Idle = 1,
    Scanning = 2,
    AccessPoint = 3,
    AccessPointDisconnecting = 4,
    PoweringUp = 5,
    Connecting = 6,
    Checking = 7,
    Connected = 8,
    Disconnecting = 9,
};
constexpr std::size_t WifiStateCount = 10;
const char* to_string(WifiState value) noexcept;

// DROM 0x3f412b08; observed bound in 0x400d5dbc.
enum class Animation : std::uint32_t {
    AllOff = 0,
    AllOn = 1,
    SteadyGreen = 2,
    SteadyYellow = 3,
    SteadyRed = 4,
    FastGreen = 5,
    FastYellow = 6,
    FastRed = 7,
    SlowGreen = 8,
    SlowYellow = 9,
    SlowRed = 10,
    Busy = 11,
    BusyRolling = 12,
    Booting = 13,
    WaitingSsid = 14,
    Updating = 15,
    WaitingPico = 16,
    Ready = 17,
    ProgramRunning = 18,
    UnhandledError = 19,
    WifiSsidNotFound = 20,
    WifiInvalidCredentials = 21,
    WifiConnectingTimeout = 22,
    WifiNoInternet = 23,
    FanFailure = 24,
    SensorOutOfRange = 25,
    SensorFailure = 26,
    PressureLeak = 27,
    NoVacuumSeal = 28,
    PumpClogged = 29,
    HardwareNotConnected = 30,
    SensorConnectionWrong = 31,
    FanConnectionWrong = 32,
    PressureLeak_HardwareNotConnected = 33,
    NoVacuumSeal_HardwareNotConnected = 34,
    PumpClogged_HardwareNotConnected = 35,
    PressureLeak_ConnectionWrong = 36,
    NoVacuumSeal_ConnectionWrong = 37,
    PumpClogged_ConnectionWrong = 38,
};
constexpr std::size_t AnimationCount = 39;
const char* to_string(Animation value) noexcept;

}} // namespace picostill::oem
