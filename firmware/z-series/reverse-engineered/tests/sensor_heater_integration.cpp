#include "zseries/sensor_acquisition.hpp"
#include <array>
#include <cassert>
#include <cstdint>
using namespace zseries::oem;

namespace {
struct Bus {
    std::array<int, 8> bytes{{8, 0, 8, 0, 8, 0, 8, 0}};
    std::size_t returned = 8, position = 0, requests = 0;
    std::uint32_t now = 1000, clock_calls = 0;
    bool output = false;
};
void begin(void*, std::uint8_t) {}
std::size_t write(void*, std::uint8_t) { return 1; }
std::uint8_t end(void*, bool) { return 0; }
void delay(void*, std::uint32_t) {}
std::size_t request(void* p, std::uint8_t address, std::size_t count, bool stop) {
    auto& bus = *static_cast<Bus*>(p);
    assert(address == 0x34 && count == 8 && stop);
    bus.position = 0; ++bus.requests; return bus.returned;
}
int read(void* p) { auto& bus = *static_cast<Bus*>(p); return bus.bytes[bus.position++]; }
std::uint32_t clock(void* p) { auto& bus = *static_cast<Bus*>(p); ++bus.clock_calls; return ++bus.now; }
void output(void* p, std::uint8_t pin, bool value) {
    assert(pin == OriginalHeaterOutputPin); static_cast<Bus*>(p)->output = value;
}
}

int main() {
    Bus bus;
    I2cTransport io = {&bus, begin, write, end, request, read, delay};
    AcquisitionClock time = {&bus, clock};
    TemperatureAcquisition acquisition;
    HeaterSensors sensors;
    assert(max11613_setup(acquisition.adc, BoilerType::Big, io) == AcquisitionResult::Ok);
    assert(acquire_temperatures(acquisition, sensors, 1000, io, time) == AcquisitionResult::Ok);
    assert(bus.requests == 1 && bus.clock_calls == 4);
    for (unsigned i = 0; i < 4; ++i) {
        assert(sensors.temperatures[i].updated_ms == 1000);
        assert(acquisition.smoothers[i].value_ms == 1001 + i);
        assert(sensors.temperatures[i].celsius == acquisition.smoothers[i].value);
    }
    HeaterController heater;
    ThermalSafety safety; safety.enabled = true;
    HeaterIo gpio = {&bus, nullptr, output};
    assert(heater_configure_boiler(heater, BoilerType::Big));
    assert(heater_set_target(heater, 60, sensors.estimated_boil_celsius));
    heater_start(heater, sensors.temperatures[1].celsius);
    assert(heater_tick_1s(heater, sensors, safety, original_thermal_limits(), 1000, gpio));
    assert(safety.too_old == 0 && safety.too_hot == 0 && bus.output);

    // Valid byte reply containing two invalid ADC channels updates only the
    // other channels; it does not make missing Hex/Wort readings look fresh.
    const auto old_hex = sensors.temperatures[0].celsius;
    const auto old_wort = sensors.temperatures[1].celsius;
    bus.bytes = {{0, 0, 15, 255, 8, 0, 8, 0}};
    assert(acquire_temperatures(acquisition, sensors, 2000, io, time) == AcquisitionResult::Ok);
    assert(bus.clock_calls == 6);
    assert(sensors.temperatures[0].updated_ms == 1000 && sensors.temperatures[0].celsius == old_hex);
    assert(sensors.temperatures[1].updated_ms == 1000 && sensors.temperatures[1].celsius == old_wort);
    assert(sensors.temperatures[2].updated_ms == 2000 && sensors.temperatures[3].updated_ms == 2000);

    bus.returned = 7;
    assert(acquire_temperatures(acquisition, sensors, 5001, io, time) == AcquisitionResult::ShortReply);
    assert(bus.clock_calls == 6);
    assert(heater_tick_1s(heater, sensors, safety, original_thermal_limits(), 5001, gpio));
    assert(heater_tick_1s(heater, sensors, safety, original_thermal_limits(), 5001, gpio));
    assert(safety.too_old == 3 && !bus.output && heater.state.output_ticks == 0);
    assert(sensors.temperatures[0].updated_ms == 1000);

    const auto requests = bus.requests;
    assert(acquire_temperatures(acquisition, sensors, 0, io, time) == AcquisitionResult::InvalidClock);
    assert(bus.requests == requests);
    time.millis = nullptr;
    assert(acquire_temperatures(acquisition, sensors, 6000, io, time) == AcquisitionResult::MissingTransport);
    assert(bus.requests == requests);
    return 0;
}
