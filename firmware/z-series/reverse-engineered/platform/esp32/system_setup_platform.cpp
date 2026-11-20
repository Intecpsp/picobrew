#include "system_setup_platform.hpp"
#include <esp_task_wdt.h>

namespace zseries { namespace oem { namespace esp32 {
namespace {
std::uint32_t clock(void*) { return ::millis(); }
void delay_ms(void*,std::uint32_t duration) { ::delay(duration); }
void assertion_log(void*,double seconds,const char* file,std::uint32_t line,const char* expression) {
    // Original diagnostic format at 0x3f403d1d; Print/Serial owns the SDK write.
    Serial.printf("\x1b[30;41m[E_][%8.2f][ASSERT][%s:%u] %s\x1b[0m\r\n",
        seconds,file,static_cast<unsigned>(line),expression);
}
bool assertion(void* opaque,std::uint32_t line,const char* expression) {
    auto& binding=*static_cast<SystemSetupBindings*>(opaque);
    return binding.diagnostics && record_assertion(*binding.diagnostics,binding.diagnostic_services,
        "System.h",line,expression)==DiagnosticResult::Ok;
}
bool perform(void* opaque,const SystemSetupAction& action,std::uintptr_t& result) {
    auto& binding=*static_cast<SystemSetupBindings*>(opaque);
    if(!binding.system)return false;
    auto& system=*binding.system;
    result=0;
    const auto& a=action.arguments;
    switch(action.effect) {
    case SystemSetupEffect::BootPartition:
        system.boot_partition=esp_ota_get_boot_partition();
        result=reinterpret_cast<std::uintptr_t>(system.boot_partition);break;
    case SystemSetupEffect::RunningPartition:
        system.running_partition=esp_ota_get_running_partition();
        result=reinterpret_cast<std::uintptr_t>(system.running_partition);break;
    case SystemSetupEffect::FactoryPartition:
        system.factory_partition=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,nullptr);
        result=reinterpret_cast<std::uintptr_t>(system.factory_partition);break;
    case SystemSetupEffect::SpiMode:system.spi.setDataMode(static_cast<std::uint8_t>(a[0]));break;
    case SystemSetupEffect::SpiFrequency:system.spi.setFrequency(static_cast<std::uint32_t>(a[0]));break;
    case SystemSetupEffect::SpiBegin:system.spi.begin(a[0],a[1],a[2],a[3]);break;
    case SystemSetupEffect::SensorBusBegin:system.sensor_bus.begin(a[0],a[1],static_cast<std::uint32_t>(a[2]));break;
    case SystemSetupEffect::ControllerBusBegin:system.controller_bus.begin(a[0],a[1],static_cast<std::uint32_t>(a[2]));break;
    case SystemSetupEffect::ScanBuses:system.scan_buses();break;
    case SystemSetupEffect::WatchdogInitialize:result=static_cast<std::uint32_t>(esp_task_wdt_init(a[0],a[1]!=0));break;
    case SystemSetupEffect::WatchdogAdd:result=static_cast<std::uint32_t>(esp_task_wdt_add(nullptr));break;
    case SystemSetupEffect::RegisterCommands:
        return binding.register_console && binding.register_console(binding.console_context,
            system_console_commands(),SystemBuzzerNamespaceToken);
    }
    return true;
}
}
SystemSetupServices system_setup_services(SystemSetupBindings& binding) noexcept {
    SystemSetupServices services;services.context=&binding;services.perform=perform;services.assertion=assertion;return services;
}
DiagnosticServices serial_diagnostic_services() noexcept {
    DiagnosticServices services;services.millis=clock;services.assertion_log=assertion_log;services.delay_ms=delay_ms;return services;
}
}}} // namespace zseries::oem::esp32
