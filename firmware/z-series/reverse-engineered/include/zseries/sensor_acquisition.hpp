// Original Z119 MAX11613 transaction and temperature acquisition pipeline.
#pragma once
#include "zseries/heater.hpp"
#include "zseries/console.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {

constexpr std::uint8_t OriginalMax11613Address = 0x34;

// A concrete byte-oriented boundary corresponding to the recovered TwoWire
// calls. The adapter binds one bus; the original ADC uses I2C peripheral 0.
struct I2cTransport {
    void* context;
    void (*begin_transmission)(void*, std::uint8_t);
    std::size_t (*write)(void*, std::uint8_t);
    std::uint8_t (*end_transmission)(void*, bool);
    std::size_t (*request_from)(void*, std::uint8_t, std::size_t, bool);
    int (*read)(void*);
    void (*delay_ms)(void*, std::uint32_t);
};

enum class AcquisitionResult {
    Ok, MissingTransport, InvalidBoiler, ShortReply, ReadFailure,
    InvalidClock, InvalidSample, InvalidSmootherState, DiagnosticFailure
};

struct Max11613 {
    std::uint8_t address = OriginalMax11613Address;
    bool polling_enabled = false;
    std::array<TempSensorType, TempSensorIdCount> sensor_types{};
    AdcChannels raw{{InvalidRawAdc, InvalidRawAdc, InvalidRawAdc, InvalidRawAdc}};
};

// Original 0x400fad30 starts a transmission and writes 0x80, delays 10 ms,
// then starts another transmission before sending 0x82,0x07 and STOP.
// There is no endTransmission call after the first byte. Preserve this
// observed transaction ordering instead of assuming that 0x80 was sent.
AcquisitionResult max11613_setup(Max11613&, BoilerType, const I2cTransport&) noexcept;
// A production diagnostic sink executes the original returning assertions
// and permits all four channels to finish. Without a sink, invalid byte and
// zero-timestamp inputs retain the legacy guarded, data-only boundary.
AcquisitionResult max11613_poll(Max11613&, const I2cTransport&,
                               const ConsoleServices* = nullptr) noexcept;

struct NoiseSmoother {
    std::array<double, 10> samples{};
    std::uint32_t next = 0;
    std::uint32_t count = 0;
    double sum = 0;
    std::uint32_t last_ms = 0;
    double value = 0;
    std::uint32_t value_ms = 0;
};

// Original 0x40110990/0x40110924: ten-sample running average; reset aggregate
// state after a gap strictly greater than 2000 ms. Old ring bytes remain.
AcquisitionResult smooth_temperature(NoiseSmoother&, double celsius,
                                     std::uint32_t clock_ms,
                                     const ConsoleServices* = nullptr) noexcept;

struct TemperatureAcquisition {
    Max11613 adc;
    std::array<NoiseSmoother, TempSensorIdCount> smoothers;
};

struct AcquisitionClock {
    void* context;
    std::uint32_t (*millis)(void*);
};

// The temperature portion of original Core::tick at 0x40120120. Valid raw
// channels alone refresh the published temperature and its Core tick time.
// Smoother timestamps use independent millis calls, as in the original.
// Invalid channels retain previous published values/times for stale checks.
AcquisitionResult acquire_temperatures(TemperatureAcquisition&, HeaterSensors&,
                                      std::uint32_t tick_ms, const I2cTransport&,
                                      const AcquisitionClock&,
                                      const ConsoleServices* = nullptr) noexcept;

}} // namespace zseries::oem
