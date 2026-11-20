// Reconstruction of the Z119 heat-exchanger sensor decision at 0x400dd9f4.
#pragma once
#include "zseries/temperature.hpp"

namespace zseries { namespace oem {

struct HexTemperatures {
    double ambient_celsius;
    double wort_celsius;
    double drain_celsius;
    double senstech_hex_celsius;
    double aet_hex_celsius;
};

struct HexDetection {
    HexDetectError error;
    // On a detection error the OEM writes numeric sentinel 4, outside the
    // four sensor enum values. Do not treat this as a fifth sensor model.
    TempSensorType sensor;
};

// Pure decision portion of 0x400dd9f4. Return false rejects values outside
// the original finite, safely integer-convertible calculation domain; out
// then remains unchanged. Return true includes OEM detection errors in out.
// The original comparisons intentionally truncate differences to int32.
bool evaluate_hex_detection(const HexTemperatures& temperatures,
                            HexDetection& out) noexcept;

// Valid ADC domain of the complete original sensor selection calculation.
// Rejects an invalid sample before any conversion. No I2C or controller I/O.
bool detect_hex_from_adc(const AdcChannels& raw, HexDetection& out) noexcept;

}} // namespace zseries::oem
