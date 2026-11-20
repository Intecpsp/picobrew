#include "zseries/sensor_acquisition.hpp"
#include <cstdint>
#include <cstring>
using namespace zseries::oem;

namespace {
struct Bus {
    std::uint32_t* events;
    std::uint32_t count = 0;
    std::uint32_t returned = 0;
    const int* bytes = nullptr;
    std::uint32_t size = 0, position = 0;
    void event(std::uint32_t kind, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0) {
        events[count * 4] = kind; events[count * 4 + 1] = a;
        events[count * 4 + 2] = b; events[count * 4 + 3] = c; ++count;
    }
};
void begin(void* p, std::uint8_t address) { static_cast<Bus*>(p)->event(0, address); }
std::size_t write(void* p, std::uint8_t value) { static_cast<Bus*>(p)->event(1, value); return 1; }
std::uint8_t end(void* p, bool stop) { static_cast<Bus*>(p)->event(2, stop); return 0; }
void delay(void* p, std::uint32_t value) { static_cast<Bus*>(p)->event(3, value); }
std::size_t request(void* p, std::uint8_t address, std::size_t count, bool stop) {
    auto& bus = *static_cast<Bus*>(p); bus.event(4, address, static_cast<std::uint32_t>(count), stop); return bus.returned;
}
int read(void* p) {
    auto& bus = *static_cast<Bus*>(p);
    const int value = bus.position < bus.size ? bus.bytes[bus.position++] : -1;
    bus.event(5, static_cast<std::uint32_t>(value)); return value;
}
}

extern "C" int adc_action(unsigned setup, std::uint32_t* words, std::uint32_t boiler,
                           std::uint32_t returned, const int* bytes, std::uint32_t byte_count,
                           std::uint32_t* events, std::uint32_t* event_count, int missing_io) {
    Max11613 adc;
    adc.address = static_cast<std::uint8_t>(words[0]); adc.polling_enabled = words[1] != 0;
    for (unsigned i = 0; i < 4; ++i) {
        adc.sensor_types[i] = static_cast<TempSensorType>(words[2 + i]);
        std::memcpy(&adc.raw[i], words + 6 + i, sizeof(std::int32_t));
    }
    Bus bus; bus.events = events; bus.returned = returned; bus.bytes = bytes; bus.size = byte_count;
    I2cTransport io = {&bus, begin, write, end, request, read, delay};
    if (missing_io) io = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    const auto result = setup ? max11613_setup(adc, static_cast<BoilerType>(boiler), io) : max11613_poll(adc, io);
    words[0] = adc.address; words[1] = adc.polling_enabled;
    for (unsigned i = 0; i < 4; ++i) {
        words[2 + i] = static_cast<std::uint32_t>(adc.sensor_types[i]);
        std::memcpy(words + 6 + i, &adc.raw[i], sizeof(std::int32_t));
    }
    *event_count = bus.count;
    return static_cast<int>(result);
}

extern "C" int smoother_action(double* values, std::uint32_t* words, double input, std::uint32_t now) {
    NoiseSmoother state;
    for (unsigned i = 0; i < 10; ++i) state.samples[i] = values[i];
    state.sum = values[10]; state.value = values[11];
    state.next = words[0]; state.count = words[1]; state.last_ms = words[2]; state.value_ms = words[3];
    const auto result = smooth_temperature(state, input, now);
    for (unsigned i = 0; i < 10; ++i) values[i] = state.samples[i];
    values[10] = state.sum; values[11] = state.value;
    words[0] = state.next; words[1] = state.count; words[2] = state.last_ms; words[3] = state.value_ms;
    return static_cast<int>(result);
}
