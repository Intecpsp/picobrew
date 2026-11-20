#include "esp32_platform.hpp"
#include <esp_task_wdt.h>

namespace zseries { namespace oem { namespace esp32 {
namespace {
void configure_output(void*, std::uint8_t pin) { pinMode(pin, OUTPUT); }
void configure_input(void*, std::uint8_t pin) { pinMode(pin, INPUT); }
int read_pin(void*, std::uint8_t pin) { return digitalRead(pin); }
void write_pin(void*, std::uint8_t pin, bool high) { digitalWrite(pin, high ? HIGH : LOW); }
void begin(void* context, std::uint8_t address) { static_cast<TwoWire*>(context)->beginTransmission(address); }
std::size_t write(void* context, std::uint8_t value) { return static_cast<TwoWire*>(context)->write(value); }
std::uint8_t end(void* context, bool stop) { return static_cast<TwoWire*>(context)->endTransmission(stop); }
std::size_t request(void* context, std::uint8_t address, std::size_t count, bool stop) {
    return static_cast<TwoWire*>(context)->requestFrom(address, static_cast<std::uint8_t>(count), static_cast<std::uint8_t>(stop));
}
int read(void* context) { return static_cast<TwoWire*>(context)->read(); }
void delay_ms(void*, std::uint32_t value) { delay(value); }
std::uint32_t clock_ms(void*) { return millis(); }

void msp_transmit(void* context, std::uint8_t address, const std::uint8_t* bytes, std::size_t count) {
    auto& wire = *static_cast<TwoWire*>(context);
    wire.beginTransmission(address);
    for (std::size_t index = 0; index < count; ++index) wire.write(bytes[index]);
    wire.endTransmission(true);
}

void msp_receive(void* context, std::uint8_t address, std::uint8_t* bytes, std::size_t count) {
    auto& wire = *static_cast<TwoWire*>(context);
    wire.requestFrom(address, static_cast<std::uint8_t>(count), static_cast<std::uint8_t>(true));
    // The original 0x400dbd2c has no deadline. This is intentionally the OEM
    // adapter; a later modified firmware must specify any timeout policy.
    while (static_cast<std::size_t>(wire.available()) < count) yield();
    for (std::size_t index = 0; index < count; ++index) bytes[index] = static_cast<std::uint8_t>(wire.read());
}

void scan(TwoWire& wire, const std::uint8_t* expected, std::size_t count, BusPresence& result) {
    result = BusPresence{};
    // Original 0x40112dec tests every 7-bit address, including 0 and 127.
    for (unsigned address = 0; address < result.found.size(); ++address) {
        wire.beginTransmission(static_cast<std::uint8_t>(address));
        result.found[address] = wire.endTransmission(true) == 0;
    }
    for (std::size_t index = 0; index < count; ++index)
        if (!result.found[expected[index]]) result.missing[result.missing_count++] = expected[index];
}
} // namespace

System::System() : spi(2), sensor_bus(Wire), controller_bus(Wire1) {}

esp_err_t System::setup() {
    boot_partition = esp_ota_get_boot_partition();
    running_partition = esp_ota_get_running_partition();
    factory_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    // Preserve the order at 0x40112f88, including setters before SPI::begin.
    spi.setDataMode(0);
    spi.setFrequency(4000000);
    spi.begin(17, 5, 16, -1);
    sensor_bus.begin(22, 23, 400000);
    controller_bus.begin(25, 26, 400000);
    const std::uint8_t sensor_addresses[] = {0x19, 0x34, 0x76};
    const std::uint8_t controller_addresses[] = {0x40};
    scan(sensor_bus, sensor_addresses, 3, sensors);
    scan(controller_bus, controller_addresses, 1, controller);
    const esp_err_t initialized = esp_task_wdt_init(45, true);
    if (initialized != ESP_OK) return initialized;
    return esp_task_wdt_add(nullptr);
}

HeaterIo gpio() noexcept { return {nullptr, configure_output, write_pin}; }
SafetyIo safety_input() noexcept { return {nullptr, configure_input, read_pin}; }
I2cTransport i2c(TwoWire& wire) noexcept { return {&wire, begin, write, end, request, read, delay_ms}; }
AcquisitionClock acquisition_clock() noexcept { return {nullptr, clock_ms}; }
z_msp_transport msp_transport(TwoWire& wire) noexcept { return {&wire, msp_transmit, msp_receive, delay_ms}; }
int initialize_msp(z_msp_client& client, z_msp_state& state, System& system) noexcept {
    // Core constructor 0x40120a04: secondary I2C, address 0x40, poll 1000 ms,
    // transition/response delays 5 ms. This initializes the client only.
    return z_msp_init(&client, &state, msp_transport(system.controller_bus), 0x40, 1000, 5, 5);
}

bool start_core_task(TaskFunction_t entry, void* context) noexcept {
    if (!entry) return false;
    initArduino();
    return xTaskCreatePinnedToCore(entry, "coreLoopTask", 0x4000, context, 1, nullptr, 1) == pdPASS;
}
void initialize_console() { Serial.begin(115200, 0x0800001c, -1, -1, false, 20000); }

bool start_periodic_software_timer(void* context, std::uint32_t period, std::uintptr_t id,
                                    CoreTimerCallback callback, std::uint32_t block, PlatformHandle& result) {
    auto* callbacks = static_cast<TimerCallbacks*>(context);
    if (!callbacks || callback != CoreTimerCallback::Buzzer || !callbacks->buzzer) return false;
    TimerHandle_t timer = xTimerCreate("", period, pdTRUE, reinterpret_cast<void*>(id), callbacks->buzzer);
    result = reinterpret_cast<PlatformHandle>(timer);
    return timer && xTimerStart(timer, block) == pdPASS;
}
PlatformHandle begin_hardware_timer(void*, std::uint8_t id, std::uint16_t divider, bool count_up) {
    return reinterpret_cast<PlatformHandle>(timerBegin(id, divider, count_up));
}
void attach_timer_interrupt(void* context, PlatformHandle handle, CoreTimerCallback callback, bool edge) {
    auto& callbacks = *static_cast<TimerCallbacks*>(context);
    const auto target = callback == CoreTimerCallback::HeaterSecond ? callbacks.heater_second : callbacks.heater_fast;
    timerAttachInterrupt(reinterpret_cast<hw_timer_t*>(handle), target, edge);
}
void write_timer_alarm(void*, PlatformHandle handle, std::uint64_t ticks, bool reload) {
    timerAlarmWrite(reinterpret_cast<hw_timer_t*>(handle), ticks, reload);
}
void enable_timer_alarm(void*, PlatformHandle handle, bool enabled) {
    auto* timer = reinterpret_cast<hw_timer_t*>(handle);
    if (enabled) timerAlarmEnable(timer); else timerAlarmDisable(timer);
}

}}} // namespace zseries::oem::esp32
