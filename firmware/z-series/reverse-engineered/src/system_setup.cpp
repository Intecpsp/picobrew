#include "zseries/system_setup.hpp"

namespace zseries { namespace oem {
namespace {
bool effect(const SystemSetupServices& services, SystemSetupEffect kind,
            std::uintptr_t& result, std::int32_t a = 0, std::int32_t b = 0,
            std::int32_t c = 0, std::int32_t d = 0) {
    SystemSetupAction action(kind);
    action.arguments = {{a,b,c,d}};
    result = 0;
    return services.perform(services.context, action, result);
}
}
SystemSetupResult setup_system(SystemSetupState& state, const SystemSetupServices& services) {
    if (!services.perform || !services.assertion) return SystemSetupResult::MissingService;
    if (!effect(services, SystemSetupEffect::BootPartition, state.boot_partition) ||
        !effect(services, SystemSetupEffect::RunningPartition, state.running_partition) ||
        !effect(services, SystemSetupEffect::FactoryPartition, state.factory_partition))
        return SystemSetupResult::ServiceFailure;
    std::uintptr_t result = 0;
    if (!effect(services, SystemSetupEffect::SpiMode, result, 0) ||
        !effect(services, SystemSetupEffect::SpiFrequency, result, 4000000) ||
        !effect(services, SystemSetupEffect::SpiBegin, result, 17, 5, 16, -1) ||
        !effect(services, SystemSetupEffect::SensorBusBegin, result, 22, 23, 400000) ||
        !effect(services, SystemSetupEffect::ControllerBusBegin, result, 25, 26, 400000) ||
        !effect(services, SystemSetupEffect::ScanBuses, result) ||
        !effect(services, SystemSetupEffect::WatchdogInitialize, result, 45, 1))
        return SystemSetupResult::ServiceFailure;
    state.watchdog_initialize_error = static_cast<std::int32_t>(static_cast<std::uint32_t>(result));
    if (state.watchdog_initialize_error != 0 &&
        !services.assertion(services.context, 252, "ESP_OK == ret"))
        return SystemSetupResult::AssertionServiceFailure;
    if (!effect(services, SystemSetupEffect::WatchdogAdd, result, 0))
        return SystemSetupResult::ServiceFailure;
    state.watchdog_add_error = static_cast<std::int32_t>(static_cast<std::uint32_t>(result));
    if (state.watchdog_add_error != 0 &&
        !services.assertion(services.context, 254, "ESP_OK == ret"))
        return SystemSetupResult::AssertionServiceFailure;
    return effect(services, SystemSetupEffect::RegisterCommands, result)
        ? SystemSetupResult::Ok : SystemSetupResult::ServiceFailure;
}

const std::array<SystemConsoleDescriptor, 7>& system_console_commands() noexcept {
    static const std::array<SystemConsoleDescriptor, 7> commands{{
        {SystemConsoleCommand::Info, true, "i", "Display System information"},
        {SystemConsoleCommand::Scan, true, "scan", "Scan I2C buses"},
        {SystemConsoleCommand::ResetFactory, true, "reset", "Reset to firmware flashed at factory"},
        {SystemConsoleCommand::Restart, true, "restart", "Restart system"},
        {SystemConsoleCommand::Ethernet, true, "eth", "Calls ethernetConnect"},
        {SystemConsoleCommand::Alarm, false, "setalarm ", "Enable/disable alarm (`setalarm on`, `setalarm off`)"},
        {SystemConsoleCommand::WatchdogTest, true, "test wd", "Test watch dog with endless loop"}
    }};
    return commands;
}
}} // namespace zseries::oem
