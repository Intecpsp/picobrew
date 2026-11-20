#include "zseries/core_startup.hpp"
#include <cstddef>

namespace zseries { namespace oem {
namespace {
const CoreStageEvidence stages[] = {
    {CoreSetupStage::Wifi, 0x3d1c, 0x400f6ae8, "Wifi"},
    {CoreSetupStage::HttpServer, 0x3d70, 0x400f7e98, "HttpServer"},
    {CoreSetupStage::Buzzer, 0x4660, 0x400f6e78, "Buzzer"},
    {CoreSetupStage::System, 0x4680, 0x40112f88, "System"},
    {CoreSetupStage::Msp, 0x4754, 0x401007dc, "Msp"},
    {CoreSetupStage::Eeprom, 0x4788, 0x400fbc1c, "Eeprom"},
    {CoreSetupStage::Safeties, 0x4810, 0x400f7028, "Safeties"},
    {CoreSetupStage::PowerButton, 0x4890, 0x400d0364, "PowerButton"},
    {CoreSetupStage::Accelerometer, 0x48e8, 0x400de078, "LIS3DHTR"},
    {CoreSetupStage::Pressure, 0x4900, 0x400de10c, "BMP280"},
    // Constructor binds Core+0x5d58 to PicoStill's secondary interface
    // at Core+0x4a1c. Its virtual setup entry remains a required component.
    {CoreSetupStage::PicoStill, 0x4a1c, 0x400fc0a4, "PicoStill"},
    {CoreSetupStage::Head, 0x5e00, 0x400ff63c, "Head"},
    {CoreSetupStage::Encoder, 0x158d0, 0x400fb8a4, "Encoder"},
    {CoreSetupStage::ZBackend, 0xfff0, 0x40112378, "ZBackend"},
    {CoreSetupStage::Runtime, 0x12bd0, 0x4010c0a0, "Runtime"},
    {CoreSetupStage::HttpBackend, 0x13518, 0x401122a0, "HttpBackend"},
    {CoreSetupStage::SerialLogger, 0x14f58, 0x400fae40, "SerialLogger"},
    {CoreSetupStage::SessionLogger, 0x1513c, 0x400fb46c, "ZSessionLogger"},
    {CoreSetupStage::Azure, 0x15160, 0x4010dd9c, "Azure"},
    {CoreSetupStage::Pinger, 0x158b8, 0x400f71b0, "Pinger"},
    {CoreSetupStage::Diagnostics, 0, 0x4011038c, "Core diagnostics"},
    {CoreSetupStage::Information, 0, 0x400efc24, "Core information"},
};

bool available(const CoreStartupServices& services, const HeaterIo& io,
               const I2cTransport& adc) noexcept {
    return services.setup_component && services.read_boiler_type &&
        services.start_periodic_software_timer && services.begin_hardware_timer &&
        services.attach_timer_interrupt && services.write_timer_alarm && services.enable_timer_alarm &&
        services.register_heater_commands && services.register_adc_commands && services.assertion &&
        io.configure_output && io.write_pin && adc.begin_transmission && adc.write &&
        adc.end_transmission && adc.delay_ms;
}
} // namespace

const CoreStageEvidence* core_stage_evidence(CoreSetupStage stage) noexcept {
    const auto index = static_cast<std::size_t>(stage);
    return index < sizeof(stages) / sizeof(stages[0]) ? &stages[index] : nullptr;
}

bool set_core_heater_timers(const CoreTimerHandles& handles, bool enabled,
                            const CoreStartupServices& services) noexcept {
    if (!services.enable_timer_alarm || !handles.heater_second || !handles.heater_fast) return false;
    services.enable_timer_alarm(services.context, handles.heater_second, enabled);
    services.enable_timer_alarm(services.context, handles.heater_fast, enabled);
    return true;
}

CoreStartupResult setup_core_controls(TemperatureAcquisition& acquisition, HeaterController& heater,
                                      CoreTimerHandles& handles, const I2cTransport& adc,
                                      const HeaterIo& io, const CoreStartupServices& services,
                                      CoreSetupStage* failed_stage) noexcept {
    if (!available(services, io, adc)) return CoreStartupResult::MissingService;
    if (heater.state.started) return CoreStartupResult::HeaterAlreadyStarted;
    const auto setup = [&](CoreSetupStage stage) {
        const bool result = services.setup_component(services.context, stage);
        if (!result && failed_stage) *failed_stage = stage;
        return result;
    };
    io.configure_output(io.context, 33);
    io.write_pin(io.context, 33, true);
    for (std::size_t i = 0; i <= static_cast<std::size_t>(CoreSetupStage::PowerButton); ++i)
        if (!setup(static_cast<CoreSetupStage>(i))) return CoreStartupResult::ComponentFailure;
    BoilerType boiler;
    if (!services.read_boiler_type(services.context, boiler)) return CoreStartupResult::InvalidBoiler;
    const auto adc_result = max11613_setup(acquisition.adc, boiler, adc);
    if (adc_result != AcquisitionResult::Ok) return CoreStartupResult::AdcFailure;
    if (!services.register_adc_commands(services.context)) return CoreStartupResult::ComponentFailure;
    if (!setup(CoreSetupStage::Accelerometer) || !setup(CoreSetupStage::Pressure)) return CoreStartupResult::ComponentFailure;
    if (!heater_initialize_outputs(heater, io)) return CoreStartupResult::HeaterFailure;
    if (!services.register_heater_commands(services.context)) return CoreStartupResult::ComponentFailure;
    if (!services.read_boiler_type(services.context, boiler)) return CoreStartupResult::InvalidBoiler;
    if (!pid_set_output_limits(heater.pid, 0, 100, heater.state.output_ticks)) return CoreStartupResult::HeaterFailure;
    pid_set_direction(heater.pid, false);
    if (!heater_configure_boiler(heater, boiler)) return CoreStartupResult::InvalidBoiler;
    for (std::size_t i = static_cast<std::size_t>(CoreSetupStage::PicoStill);
            i <= static_cast<std::size_t>(CoreSetupStage::Pinger); ++i)
        if (!setup(static_cast<CoreSetupStage>(i))) return CoreStartupResult::ComponentFailure;
    if (!services.start_periodic_software_timer(services.context, 1000, 1,
                                                CoreTimerCallback::Buzzer, 10, handles.buzzer) &&
        !services.assertion(services.context, 0x557, "res == pdPASS"))
        return CoreStartupResult::SoftwareTimerFailure;
    handles.heater_second = services.begin_hardware_timer(services.context, 0, 80, true);
    handles.heater_fast = services.begin_hardware_timer(services.context, 1, 80, true);
    if (!handles.heater_second || !handles.heater_fast) return CoreStartupResult::HardwareTimerFailure;
    services.attach_timer_interrupt(services.context, handles.heater_second, CoreTimerCallback::HeaterSecond, true);
    services.attach_timer_interrupt(services.context, handles.heater_fast, CoreTimerCallback::HeaterFast, true);
    services.write_timer_alarm(services.context, handles.heater_second, 1000000, true);
    services.write_timer_alarm(services.context, handles.heater_fast, 20000, true);
    set_core_heater_timers(handles, true, services);
    if (!setup(CoreSetupStage::Diagnostics) || !setup(CoreSetupStage::Information)) return CoreStartupResult::ComponentFailure;
    return CoreStartupResult::Ok;
}

}} // namespace zseries::oem
