// Original Core::setup ordering and recovered control-component integration.
#pragma once
#include "zseries/sensor_acquisition.hpp"
#include <cstdint>

namespace zseries { namespace oem {

enum class CoreSetupStage : std::uint8_t {
    Wifi, HttpServer, Buzzer, System, Msp, Eeprom, Safeties, PowerButton,
    Accelerometer, Pressure, PicoStill, Head, Encoder, ZBackend, Runtime,
    HttpBackend, SerialLogger, SessionLogger, Azure, TimerMonitor,
    Diagnostics, Information
};

struct CoreStageEvidence {
    CoreSetupStage stage;
    std::uint32_t core_offset;
    std::uint32_t setup_address;
    const char* name;
};
const CoreStageEvidence* core_stage_evidence(CoreSetupStage) noexcept;

enum class CoreTimerCallback : std::uint8_t { Buzzer, HeaterSecond, HeaterFast };
using PlatformHandle = std::uintptr_t;

// Owned integration boundaries. Every callback is required. In particular,
// a component whose implementation has not been recovered must not be bound
// to a successful no-op. The source does not claim a complete Core object.
struct CoreStartupServices {
    void* context;
    bool (*setup_component)(void*, CoreSetupStage);
    bool (*read_boiler_type)(void*, BoilerType&);
    bool (*start_periodic_software_timer)(void*, std::uint32_t period_ticks,
                                          std::uintptr_t timer_id,
                                          CoreTimerCallback, std::uint32_t block_ticks,
                                          PlatformHandle& result);
    PlatformHandle (*begin_hardware_timer)(void*, std::uint8_t id, std::uint16_t divider, bool count_up);
    void (*attach_timer_interrupt)(void*, PlatformHandle, CoreTimerCallback, bool edge);
    void (*write_timer_alarm)(void*, PlatformHandle, std::uint64_t ticks, bool auto_reload);
    void (*enable_timer_alarm)(void*, PlatformHandle, bool);
};

struct CoreTimerHandles {
    PlatformHandle buzzer = 0;
    PlatformHandle heater_second = 0;
    PlatformHandle heater_fast = 0;
};

enum class CoreStartupResult {
    Ok, MissingService, ComponentFailure, InvalidBoiler, AdcFailure,
    HeaterFailure, SoftwareTimerFailure, HardwareTimerFailure,
    HeaterAlreadyStarted
};

// Original 0x40113078. ADC and heater setup use the recovered implementations;
// the remaining components are explicit interfaces until their modules are
// available. Diagnostics retain their original position after timer enabling.
CoreStartupResult setup_core_controls(TemperatureAcquisition&, HeaterController&,
                                      CoreTimerHandles&, const I2cTransport&,
                                      const HeaterIo&, const CoreStartupServices&,
                                      CoreSetupStage* failed_stage = nullptr) noexcept;

bool set_core_heater_timers(const CoreTimerHandles&, bool enabled,
                            const CoreStartupServices&) noexcept;

}} // namespace zseries::oem
