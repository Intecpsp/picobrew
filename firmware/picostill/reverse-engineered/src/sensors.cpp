#include "picostill/sensors.hpp"

namespace picostill { namespace oem {
const ThermistorParameters* board_thermistor(unsigned channel) noexcept {
    // Exact values written by original static initializer 0x400d8688 to the
    // four 40-byte records at 0x3ffc3080, a8, d0 and f8. No RAM dependency.
    static const ThermistorParameters parameters[] = {
        {0.00069284469379999995,0.00028461150380000003,4.5871588829999998e-08,10000.0,4095.0f},
        {0.00069284469379999995,0.00028461150380000003,4.5871588829999998e-08,10000.0,4095.0f},
        {0.00099885643949999998,0.0002279998788,9.9551784679999997e-08,10000.0,4095.0f},
        {0.00090174774799999999,0.00024891903099999999,2.0432138570000001e-07,10000.0,4095.0f},
    };
    return channel<4 ? &parameters[channel] : nullptr;
}
void AdcReader::clear() noexcept { raw.fill(0); valid.fill(false); }
void AdcReader::read(I2cPorts& ports) {
    clear(); // 0x400d2f9c.
    const unsigned channels=three_channels?3:4;
    const auto received=ports.request(address,channels*2);
    if(three_channels ? ports.available()<=5 : received!=8) return;
    for(unsigned i=0;i<channels;++i) {
        const auto high=static_cast<std::uint32_t>(ports.read_byte());
        const auto low=static_cast<std::uint32_t>(ports.read_byte());
        raw[i]=static_cast<std::uint16_t>(((high<<8)|low)&0xfffu); // 0x400d6ed4.
        valid[i]=raw[i]!=0; // OEM accepts full-scale 4095; no inferred clamp.
    }
}
void PressureReader::read(I2cPorts& ports) {
    ports.begin_transmission(address);ports.write_byte(0xaa);ports.write_byte(0);ports.write_byte(0);
    ports.end_transmission();ports.delay_ms(25);
    valid=true;std::uint8_t status=0;std::uint32_t raw=0;
    if(ports.request(address,4)==4) {
        status=static_cast<std::uint8_t>(ports.read_byte());
        const auto a=static_cast<std::uint32_t>(ports.read_byte());
        const auto b=static_cast<std::uint32_t>(ports.read_byte());
        const auto c=static_cast<std::uint32_t>(ports.read_byte());
        raw=(a<<16)|(b<<8)|c;
    } else valid=false;
    if(status!=expected_status) valid=false;
    // The original calculates the numeric result even after a failed read.
    const auto scaled=static_cast<std::int32_t>(static_cast<std::uint32_t>(range)*(raw-0x19999au));
    value=static_cast<double>(scaled)/13421772.0; // 0x400d68c4.
}
void Sensors::reset_metrics() noexcept {
    metrics.temperature_samples=0;metrics.pressure_samples=0;
    metrics.success_rates.fill(0);metrics.averages.fill(0);
}
void Sensors::sample_temperatures() {
    adc.read(ports_);
    for(unsigned i=0;i<4;++i) if(adc.valid[i])
        values[i]=static_cast<float>(thermistor_kelvin(*board_thermistor(i),adc.raw[i],diagnostics_)-273.15);
    if(metrics.temperature_samples>59) reset_metrics();
    ++metrics.temperature_samples;
    for(unsigned i=0;i<4;++i) {
        metrics.success_rates[i]=static_cast<float>(exponential_moving_average(metrics.success_rates[i],
            static_cast<std::int32_t>(metrics.temperature_samples),adc.valid[i]?1.0:0.0,diagnostics_));
        metrics.averages[i]=static_cast<float>(exponential_moving_average(metrics.averages[i],
            static_cast<std::int32_t>(metrics.temperature_samples),values[i],diagnostics_));
    }
}
void Sensors::sample_pressure() {
    pressure.read(ports_);
    if(pressure.valid) values[4]=static_cast<float>(pressure.value);
    if(metrics.pressure_samples>59) {metrics.temperature_samples=2;metrics.pressure_samples=2;}
    ++metrics.pressure_samples;
    metrics.success_rates[4]=static_cast<float>(exponential_moving_average(metrics.success_rates[4],
        static_cast<std::int32_t>(metrics.pressure_samples),pressure.valid?1.0:0.0,diagnostics_));
    metrics.averages[4]=static_cast<float>(exponential_moving_average(metrics.averages[4],
        static_cast<std::int32_t>(metrics.pressure_samples),values[4],diagnostics_));
}
}} // namespace picostill::oem
