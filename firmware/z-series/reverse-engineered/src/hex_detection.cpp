#include "zseries/hex_detection.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace zseries { namespace oem {
namespace {

std::int32_t whole_degree_delta(double a, double b) noexcept {
    const auto difference = static_cast<std::int32_t>(a - b);
    return difference < 0 ? -difference : difference;
}

bool matches_any_reference(double candidate, const HexTemperatures& t) noexcept {
    return whole_degree_delta(candidate, t.ambient_celsius) < 5 ||
           whole_degree_delta(candidate, t.wort_celsius) < 5 ||
           whole_degree_delta(candidate, t.drain_celsius) < 5;
}

} // namespace

bool evaluate_hex_detection(const HexTemperatures& t, HexDetection& out) noexcept {
    const double values[] = {t.ambient_celsius, t.wort_celsius, t.drain_celsius,
                             t.senstech_hex_celsius, t.aet_hex_celsius};
    double minimum = values[0], maximum = values[0];
    for (double value : values) {
        if (!std::isfinite(value)) return false;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (maximum - minimum > std::numeric_limits<std::int32_t>::max()) return false;

    HexDetection result = {HexDetectError::None,
                           static_cast<TempSensorType>(TempSensorTypeCount)};
    // The order of errors is observable in the original function.
    if (t.ambient_celsius > 35.0) result.error = HexDetectError::AmbientAboveMax;
    else if (t.wort_celsius > 35.0) result.error = HexDetectError::WortAboveMax;
    else if (t.drain_celsius > 35.0) result.error = HexDetectError::DrainAboveMax;
    else if (whole_degree_delta(t.ambient_celsius, t.wort_celsius) > 5)
        result.error = HexDetectError::AmbientWortDelta;
    else if (whole_degree_delta(t.wort_celsius, t.drain_celsius) > 5)
        result.error = HexDetectError::WortDrainDelta;
    else if (whole_degree_delta(t.drain_celsius, t.ambient_celsius) > 5)
        result.error = HexDetectError::DrainAmbientDelta;
    else {
        const bool senstech_valid = matches_any_reference(t.senstech_hex_celsius, t);
        const bool aet_valid = matches_any_reference(t.aet_hex_celsius, t);
        if (senstech_valid == aet_valid) {
            result.error = senstech_valid ? HexDetectError::BothHexValid :
                                           HexDetectError::BothHexInvalid;
        } else {
            result.sensor = senstech_valid ? TempSensorType::SENSTECH_10_366 :
                                            TempSensorType::AET_14015;
        }
    }
    out = result;
    return true;
}

bool detect_hex_from_adc(const AdcChannels& raw, HexDetection& out) noexcept {
    for (std::int32_t value : raw) if (!raw_adc_valid(value)) return false;
    const auto convert = [&raw](TempSensorId id, TempSensorType type) {
        return thermistor_celsius(*thermistor_parameters(type),
                                  raw[static_cast<std::size_t>(id)]);
    };
    const HexTemperatures temperatures = {
        convert(TempSensorId::Ambient, TempSensorType::MURATA_NCU18),
        convert(TempSensorId::Wort, TempSensorType::SENSTECH_10_364_5),
        convert(TempSensorId::Drain, TempSensorType::SENSTECH_10_364_5),
        convert(TempSensorId::Hex, TempSensorType::SENSTECH_10_366),
        convert(TempSensorId::Hex, TempSensorType::AET_14015),
    };
    return evaluate_hex_detection(temperatures, out);
}

}} // namespace zseries::oem
