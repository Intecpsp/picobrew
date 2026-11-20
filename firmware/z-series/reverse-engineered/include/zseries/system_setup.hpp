// Original Z119 System::setup ordering, 0x40112f88.
#pragma once
#include <array>
#include <cstdint>

namespace zseries { namespace oem {
enum class SystemSetupEffect : std::uint8_t {
    BootPartition, RunningPartition, FactoryPartition, SpiMode, SpiFrequency,
    SpiBegin, SensorBusBegin, ControllerBusBegin, ScanBuses,
    WatchdogInitialize, WatchdogAdd, RegisterCommands
};
struct SystemSetupAction {
    SystemSetupEffect effect;
    std::array<std::int32_t, 4> arguments{};
    explicit SystemSetupAction(SystemSetupEffect effect_) noexcept : effect(effect_) {}
};
struct SystemSetupState {
    std::uintptr_t boot_partition = 0, running_partition = 0, factory_partition = 0;
    std::int32_t watchdog_initialize_error = 0, watchdog_add_error = 0;
};
struct SystemSetupServices {
    void* context = nullptr;
    // Adapter completion is separate from the original SDK return value.
    bool (*perform)(void*, const SystemSetupAction&, std::uintptr_t& result) = nullptr;
    bool (*assertion)(void*, std::uint32_t line, const char* expression) = nullptr;
};
enum class SystemSetupResult { Ok, MissingService, ServiceFailure, AssertionServiceFailure };

// Both watchdog errors are diagnosed independently. Neither returning assertion
// suppresses the following watchdog-add or console-registration operation.
SystemSetupResult setup_system(SystemSetupState&, const SystemSetupServices&);

enum class SystemConsoleCommand : std::uint8_t { Info, Scan, ResetFactory, Restart, Ethernet, Alarm, WatchdogTest };
struct SystemConsoleDescriptor {
    SystemConsoleCommand command;
    bool exact_match;
    const char* token;
    const char* description;
};
const std::array<SystemConsoleDescriptor, 7>& system_console_commands() noexcept;
constexpr const char* SystemBuzzerNamespaceToken = "b ";

}} // namespace zseries::oem
