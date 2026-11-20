#include "zseries/auxiliary_sensors.hpp"
#include <cstring>

namespace zseries { namespace oem {
namespace {
bool available(const I2cTransport& io) noexcept {
    return io.begin_transmission && io.write && io.end_transmission && io.request_from && io.read;
}
std::uint16_t unsigned16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | static_cast<unsigned>(p[1]) << 8);
}
std::int16_t signed16(const std::uint8_t* p) noexcept {
    const auto value = unsigned16(p); std::int16_t result; std::memcpy(&result, &value, sizeof result); return result;
}
// The Xtensa sequence is two's-complement 64-bit arithmetic. Explicit
// unsigned wrapping avoids host C++ overflow/negative-left-shift undefined
// behavior while preserving the original results for all coefficient bits.
std::uint64_t bits(std::int64_t x) noexcept { return static_cast<std::uint64_t>(x); }
std::int64_t signed64(std::uint64_t x) noexcept {
    std::int64_t result; std::memcpy(&result, &x, sizeof result); return result;
}
std::uint64_t asr(std::uint64_t x, unsigned shift) noexcept {
    return (x >> shift) | ((x & (std::uint64_t(1) << 63)) ? (~std::uint64_t(0) << (64 - shift)) : 0);
}
std::int32_t low_signed32(std::uint64_t x) noexcept {
    const auto low = static_cast<std::uint32_t>(x); std::int32_t result; std::memcpy(&result, &low, sizeof result); return result;
}
std::uint64_t divide(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    const bool negative = ((numerator ^ denominator) >> 63) != 0;
    const auto n = numerator >> 63 ? -numerator : numerator;
    const auto d = denominator >> 63 ? -denominator : denominator;
    const auto quotient = n / d;
    return negative ? -quotient : quotient;
}
void write_register(std::uint8_t address, std::uint8_t reg, std::uint8_t value,
                    const I2cTransport& io) noexcept {
    io.begin_transmission(io.context, address);
    io.write(io.context, reg); io.write(io.context, value);
    (void)io.end_transmission(io.context, true); // Original setup ignores this result.
}
} // namespace

std::uint32_t read_auxiliary_register(std::uint8_t address, std::uint8_t reg,
                                     std::uint8_t* destination, std::uint8_t count,
                                     const I2cTransport& io) noexcept {
    if (!available(io) || (!destination && count)) return AuxiliaryMissingService;
    io.begin_transmission(io.context, address);
    io.write(io.context, static_cast<std::uint8_t>(reg | 0x80));
    if (io.end_transmission(io.context, true) != 0) return AuxiliaryTransmitFailure;
    if (io.request_from(io.context, address, count, true) != count) return AuxiliaryShortReply;
    for (std::size_t i = 0; i < count; ++i) destination[i] = static_cast<std::uint8_t>(io.read(io.context));
    return AuxiliaryOk;
}

std::uint32_t lis3dhtr_setup(Lis3dhtr& device, const I2cTransport& io, const ConsoleServices* diagnostic) noexcept {
    if (!available(io)) return AuxiliaryMissingService;
    const std::uint8_t values[] = {0, 0x47, 0, 0, 0x88, 0, 0};
    for (std::uint8_t i = 0; i < 7; ++i) write_register(device.address, 0x1f + i, values[i], io);
    std::uint8_t id = 0;
    const auto result = read_auxiliary_register(device.address, 0x0f, &id, 1, io);
    if (result != AuxiliaryOk) return result;
    if (id != 0x33) {
        if (diagnostic && (!diagnostic->assertion || !diagnostic->assertion(diagnostic->context,"LIS3DHTR.h",0x8f,"idCheck == 0x33")))
            return AuxiliaryWrongId | AuxiliaryDiagnosticFailure;
        return AuxiliaryWrongId;
    }
    device.polling_enabled = true;
    return AuxiliaryOk;
}

std::uint32_t lis3dhtr_tick(Lis3dhtr& device, AuxiliarySensors& sensors, const I2cTransport& io) noexcept {
    if (!available(io)) return AuxiliaryMissingService;
    if (!device.polling_enabled) return AuxiliaryOk;
    std::uint8_t bytes[6];
    const auto result = read_auxiliary_register(device.address, 0x28, bytes, 6, io);
    if (result == AuxiliaryOk)
        for (std::size_t i = 0; i < 3; ++i) device.raw[i] = signed16(bytes + 2 * i);
    const double x = static_cast<double>(device.raw[0]) / 15987.0;
    const double y = static_cast<double>(device.raw[1]) / 15987.0;
    const double z = static_cast<double>(device.raw[2]) / 15987.0;
    sensors.acceleration[0] = x * 0.002 + sensors.acceleration[0] * 0.998;
    sensors.acceleration[1] = sensors.acceleration[1] * 0.998 + y * 0.002;
    sensors.acceleration[2] = sensors.acceleration[2] * 0.998 + z * 0.002;
    return result;
}

std::uint32_t bmp280_read_calibration(Bmp280& device, const I2cTransport& io) noexcept {
    std::uint8_t bytes[24];
    const auto result = read_auxiliary_register(device.address, 0x88, bytes, 24, io);
    if (result != AuxiliaryOk) return result;
    auto& c = device.calibration;
    c.t1 = unsigned16(bytes); c.t2 = signed16(bytes + 2); c.t3 = signed16(bytes + 4);
    c.p1 = unsigned16(bytes + 6); c.p2 = signed16(bytes + 8); c.p3 = signed16(bytes + 10);
    c.p4 = signed16(bytes + 12); c.p5 = signed16(bytes + 14); c.p6 = signed16(bytes + 16);
    c.p7 = signed16(bytes + 18); c.p8 = signed16(bytes + 20); c.p9 = signed16(bytes + 22);
    return AuxiliaryOk;
}

std::uint32_t bmp280_setup(Bmp280& device, const I2cTransport& io, const ConsoleServices* diagnostic) noexcept {
    if (!available(io)) return AuxiliaryMissingService;
    write_register(device.address, 0xf4, 0x3f, io);
    std::uint8_t id = 0;
    auto result = read_auxiliary_register(device.address, 0xd0, &id, 1, io);
    if (result == AuxiliaryOk) {
        if (id == 0x58) device.polling_enabled = true;
        else {
            result = AuxiliaryWrongId;
            if (diagnostic && (!diagnostic->assertion || !diagnostic->assertion(diagnostic->context,"BMP280.h",0x102,"idCheck == 0x58")))
                return result | AuxiliaryDiagnosticFailure;
        }
    }
    const auto calibrated = bmp280_read_calibration(device, io);
    if (calibrated != AuxiliaryOk && diagnostic && (!diagnostic->log ||
        !diagnostic->log(diagnostic->context,ConsoleLevel::Error,"BMP","BMP280.h",0x10a,"Failed to retrieve calibration data")))
        return result | calibrated | AuxiliaryDiagnosticFailure;
    return result | calibrated;
}

std::uint32_t original_bmp280_raw(const std::uint8_t bytes[3]) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 12) |
           (static_cast<std::uint32_t>(bytes[1]) << 4) | (bytes[0] >> 4);
}

double bmp280_compensate(Bmp280& device, bool& zero_divisor) noexcept {
    const auto& c = device.calibration;
    const auto raw_t = bits(device.raw_temperature);
    const auto difference = asr(raw_t, 4) - c.t1;
    const auto t1 = asr((asr(raw_t, 3) - std::uint64_t(c.t1) * 2) * bits(c.t2), 11);
    const auto t2 = asr(asr(difference * difference, 12) * bits(c.t3), 14);
    device.temperature_fine = low_signed32(t1 + t2);
    auto v1 = bits(device.temperature_fine) - 128000;
    auto v2 = v1 * v1 * bits(c.p6);
    v2 += (v1 * bits(c.p5)) << 17;
    v2 += bits(c.p4) << 35;
    v1 = asr(v1 * v1 * bits(c.p3), 8) + ((v1 * bits(c.p2)) << 12);
    v1 = asr(((std::uint64_t(1) << 47) + v1) * c.p1, 33);
    zero_divisor = v1 == 0;
    if (zero_divisor) return 0;
    auto pressure = std::uint64_t(1048576) - bits(device.raw_pressure);
    pressure = divide(((pressure << 31) - v2) * 3125, v1);
    v1 = asr(bits(c.p9) * asr(pressure, 13) * asr(pressure, 13), 25);
    v2 = asr(bits(c.p8) * pressure, 19);
    pressure = asr(pressure + v1 + v2, 8) + (bits(c.p7) << 4);
    // Original __floatdisf, single-precision scale, then __extendsfdf2.
    const float rounded = static_cast<float>(signed64(pressure));
    return static_cast<double>(rounded * 0.00390625f);
}

std::uint32_t bmp280_tick(Bmp280& device, AuxiliarySensors& sensors, HeaterSensors& heater,
                         const I2cTransport& io, const AcquisitionClock& clock, const ConsoleServices* diagnostic) noexcept {
    if (!available(io) || !clock.millis) return AuxiliaryMissingService;
    if (!device.polling_enabled) return AuxiliaryOk;
    std::uint8_t bytes[3];
    const auto pressure_result = read_auxiliary_register(device.address, 0xf7, bytes, 3, io);
    if (pressure_result == AuxiliaryOk) device.raw_pressure = original_bmp280_raw(bytes);
    const auto temperature_result = read_auxiliary_register(device.address, 0xfa, bytes, 3, io);
    if (temperature_result == AuxiliaryOk) device.raw_temperature = original_bmp280_raw(bytes);
    bool zero_divisor = false;
    sensors.pressure_pa = bmp280_compensate(device, zero_divisor);
    if (zero_divisor && diagnostic && (!diagnostic->log ||
        !diagnostic->log(diagnostic->context,ConsoleLevel::Error,"BMP","BMP280.h",0xe2,"Invalid value")))
        return pressure_result | temperature_result | AuxiliaryZeroDivisor | AuxiliaryDiagnosticFailure;
    sensors.pressure_ms = clock.millis(clock.context);
    heater.estimated_boil_celsius = (sensors.pressure_pa - 101325.0) * 0.00034724743 + 100.0;
    return pressure_result | temperature_result | (zero_divisor ? AuxiliaryZeroDivisor : AuxiliaryOk);
}
}} // namespace zseries::oem
