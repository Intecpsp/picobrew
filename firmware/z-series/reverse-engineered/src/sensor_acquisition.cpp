#include "zseries/sensor_acquisition.hpp"
#include <cmath>

namespace zseries { namespace oem {

AcquisitionResult max11613_setup(Max11613& adc, BoilerType boiler,
                                 const I2cTransport& bus) noexcept {
    if (!bus.begin_transmission || !bus.write || !bus.end_transmission || !bus.delay_ms)
        return AcquisitionResult::MissingTransport;
    TempSensorType hex;
    if (!hex_sensor_for_boiler(boiler, hex)) return AcquisitionResult::InvalidBoiler;
    adc.sensor_types = {{hex, static_cast<TempSensorType>(1),
                        static_cast<TempSensorType>(1), static_cast<TempSensorType>(2)}};
    bus.begin_transmission(bus.context, adc.address);
    bus.write(bus.context, 0x80);
    bus.delay_ms(bus.context, 10);
    bus.begin_transmission(bus.context, adc.address);
    bus.write(bus.context, 0x82);
    bus.write(bus.context, 0x07);
    bus.end_transmission(bus.context, true);
    adc.raw.fill(InvalidRawAdc);
    adc.polling_enabled = true;
    return AcquisitionResult::Ok;
}

AcquisitionResult max11613_poll(Max11613& adc, const I2cTransport& bus) noexcept {
    if (!bus.request_from || !bus.read) return AcquisitionResult::MissingTransport;
    adc.raw.fill(InvalidRawAdc);
    if (bus.request_from(bus.context, adc.address, 8, true) != 8)
        return AcquisitionResult::ShortReply;
    for (auto& value : adc.raw) {
        const int high = bus.read(bus.context);
        const int low = bus.read(bus.context);
        // The OEM asserts here. The owned boundary reports the failure and
        // stops at the same channel; callers must not publish a partial read.
        if (high < 0 || low < 0) return AcquisitionResult::ReadFailure;
        value = static_cast<std::int32_t>((static_cast<std::uint32_t>(high) << 8 |
                                         static_cast<std::uint32_t>(low)) & 0xfffU);
    }
    return AcquisitionResult::Ok;
}

AcquisitionResult smooth_temperature(NoiseSmoother& state, double celsius,
                                     std::uint32_t clock_ms) noexcept {
    if (!std::isfinite(celsius) || !std::isfinite(state.sum)) return AcquisitionResult::InvalidSample;
    if (state.count > state.samples.size() || state.next >= state.samples.size())
        return AcquisitionResult::InvalidSmootherState;
    if (state.last_ms != 0 && clock_ms - state.last_ms > 2000U) {
        state.next = state.count = 0;
        state.sum = state.value = 0.0;
        state.value_ms = 0;
    }
    state.last_ms = clock_ms;
    if (state.count < state.samples.size()) ++state.count;
    else state.sum -= state.samples[state.next];
    state.samples[state.next] = celsius;
    if (++state.next == state.samples.size()) state.next = 0;
    state.sum += celsius;
    state.value = state.sum / static_cast<double>(state.count);
    state.value_ms = state.last_ms;
    return AcquisitionResult::Ok;
}

AcquisitionResult acquire_temperatures(TemperatureAcquisition& state, HeaterSensors& sensors,
                                      std::uint32_t tick_ms, const I2cTransport& bus,
                                      const AcquisitionClock& clock) noexcept {
    if (!state.adc.polling_enabled) return AcquisitionResult::Ok;
    if (!clock.millis) return AcquisitionResult::MissingTransport;
    // Original asserts now!=0 before publishing a valid channel. Rejecting
    // it up front is an explicit owned boundary, with no bus/state mutation.
    if (tick_ms == 0) return AcquisitionResult::InvalidClock;
    const auto polled = max11613_poll(state.adc, bus);
    if (polled != AcquisitionResult::Ok) return polled;
    for (std::size_t index = 0; index < state.adc.raw.size(); ++index) {
        if (!raw_adc_valid(state.adc.raw[index])) continue;
        const auto* calibration = thermistor_parameters(state.adc.sensor_types[index]);
        if (!calibration) return AcquisitionResult::InvalidSample;
        const double celsius = thermistor_celsius(*calibration, state.adc.raw[index]);
        const auto smoothed = smooth_temperature(state.smoothers[index], celsius, clock.millis(clock.context));
        if (smoothed != AcquisitionResult::Ok) return smoothed;
        sensors.temperatures[index].celsius = state.smoothers[index].value;
        sensors.temperatures[index].updated_ms = tick_ms;
    }
    return AcquisitionResult::Ok;
}

}} // namespace zseries::oem
