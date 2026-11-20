#include "zseries/program_repository.hpp"
#include "zseries/program_diagnostics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace zseries { namespace oem {
namespace {
void push(ProgramBuffer& buffer, OpCode op, std::uint32_t arg = 0) noexcept {
    // The original writer logs capacity overflow and continues its caller.
    (void)buffer.append({op, arg});
}
bool integer(double value, std::int32_t& result) noexcept {
    if (!std::isfinite(value) || value < -2147483648.0 || value >= 2147483648.0) return false;
    result = static_cast<std::int32_t>(value); return true;
}
}

// 0x400ec670. Metadata changes follow the same order as the original writer.
ProgramBuildResult finish_program(ProgramBuffer& buffer, const ProgramParameters& parameters,
        double pressure, double maximum_temperature, std::uint32_t added_seconds,const ConsoleServices* diagnostics) noexcept {
    if (static_cast<std::uint32_t>(parameters.water.unit) > 1) {
        buffer.assertion_line = 0x278; return ProgramBuildResult::InvalidVolume;
    }
    buffer.parameters = parameters;
    double liters = 0, rate = 0;
    (void)quantity_in(parameters.water, Unit::Liter, liters);
    if (!program_heating_rate(liters, rate)) { buffer.assertion_line = 0x2b; return ProgramBuildResult::InvalidVolume; }
    buffer.stats.heating_rate_celsius_per_minute = rate;
    const auto& program = buffer.record().view;
    buffer.stats.duration_seconds = estimate_instructions_seconds(program.code, program.code_size, false, 0, 0, 12.77, rate);
    if(diagnostics&&!report_program_estimate(*diagnostics,buffer.record(),rate,buffer.stats.duration_seconds))return ProgramBuildResult::DiagnosticFailure;
    buffer.stats.pressure_pascal = pressure;
    buffer.stats.maximum_temperature_celsius = maximum_temperature;
    buffer.stats.maximum_temperature_added_seconds = added_seconds;
    if(diagnostics&&!report_program_buffer(*diagnostics,buffer))return ProgramBuildResult::DiagnosticFailure;
    return ProgramBuildResult::Ok;
}

// 0x400e423c. These screen IDs and common-string references are original data.
ProgramBuildResult begin_coffee_program(ProgramBuffer& buffer, const char* name) noexcept {
    if (!name || !*name) { buffer.assertion_line = 0x1d4; return ProgramBuildResult::EmptyString; }
    const ProgramBuildResult result = buffer.start(SessionType::Coffee, 0, name);
    if (result != ProgramBuildResult::Ok) return result;
    push(buffer, OpCode::ValveMode, 0); push(buffer, OpCode::StepName, 0x8002);
    for (std::uint32_t screen = 0x52; screen <= 0x58; ++screen) push(buffer, OpCode::ShowScreen, screen);
    push(buffer, OpCode::RequireFilter, 1); return ProgramBuildResult::Ok;
}
// 0x400d7568, including the original 0x30e3 fixed-point temperature.
void append_cold_coffee_steps(ProgramBuffer& buffer) noexcept {
    push(buffer, OpCode::MoveValve, 2); push(buffer, OpCode::StepName, 0x8014);
    push(buffer, OpCode::CircFor, 180); push(buffer, OpCode::StepName, 0x800a);
    push(buffer, OpCode::ReachTarget, 0x30e3); push(buffer, OpCode::StepName, 0x800b);
    push(buffer, OpCode::HoldFor, 5400); push(buffer, OpCode::StepName, 0x800e);
    push(buffer, OpCode::DrainFor, 600);
}
// 0x400e4380. Integer Fahrenheit and uint32 minute-to-second wrap are retained.
ProgramBuildResult append_hot_coffee_steps(ProgramBuffer& buffer, std::int32_t fahrenheit, std::int32_t minutes) noexcept {
    if (fahrenheit < 50) { buffer.assertion_line = 0x1fc; return ProgramBuildResult::InvalidTemperature; }
    if (fahrenheit > 205) { buffer.assertion_line = 0x1fd; return ProgramBuildResult::InvalidTemperature; }
    push(buffer, OpCode::MoveValve, 1); push(buffer, OpCode::StepName, 0x800a);
    const auto fixed = static_cast<std::uint32_t>((static_cast<double>(fahrenheit - 32) / 1.8) * 256);
    push(buffer, OpCode::ReachTarget, fixed); push(buffer, OpCode::StepName, 0x800c);
    push(buffer, OpCode::HoldFor, 300); push(buffer, OpCode::MoveValve, 2);
    push(buffer, OpCode::StepName, 0x800b); push(buffer, OpCode::HoldFor, static_cast<std::uint32_t>(minutes) * 60u);
    push(buffer, OpCode::StepName, 0x800e); push(buffer, OpCode::DrainFor, 600);
    return ProgramBuildResult::Ok;
}
// 0x400e42d0; the original clamps the truncated Intensity to 1..15 minutes.
ProgramBuildResult coffee_intensity_minutes(const Quantity& quantity, std::int32_t& result) noexcept {
    if (quantity.unit != Unit::Intensity) return ProgramBuildResult::InvalidUnit;
    std::int32_t value = 0;
    if (!integer(quantity.value, value)) return ProgramBuildResult::InvalidNumericDomain;
    result = std::min<std::int32_t>(15, std::max<std::int32_t>(1, value)); return ProgramBuildResult::Ok;
}

// 0x400ec7b4 and 0x400ec740. Formatting delegates to the platform C library.
ProgramBuildResult build_coffee_program(ProgramBuffer& buffer, const ProgramParameters& parameters,
        double pressure, bool imperial) noexcept {
    const auto id = parameters.id;
    if (id != ZProgramId::CoffeeColdBrew && id != ZProgramId::CoffeeHybrid &&
        id != ZProgramId::CoffeeHot && id != ZProgramId::CoffeeCustom) return ProgramBuildResult::InvalidProgramId;
    buffer.reset();
    std::int32_t minutes = 0, fahrenheit = 0;
    if (id == ZProgramId::CoffeeHot || id == ZProgramId::CoffeeHybrid) {
        const ProgramBuildResult result = coffee_intensity_minutes(parameters.intensity, minutes);
        if (result != ProgramBuildResult::Ok) { if (result == ProgramBuildResult::InvalidUnit) buffer.assertion_line = 0x1c1; return result; }
        fahrenheit = id == ZProgramId::CoffeeHot ? 205 : 200;
    } else if (id == ZProgramId::CoffeeCustom) {
        if (!integer(parameters.time.value / 60, minutes)) return ProgramBuildResult::InvalidNumericDomain;
        double converted = 0;
        if (!quantity_in(parameters.temperature, Unit::Fahrenheit, converted)) { buffer.assertion_line = 0x7d; return ProgramBuildResult::InvalidUnit; }
        if (!integer(converted, fahrenheit)) return ProgramBuildResult::InvalidNumericDomain;
    }
    double water = 0;
    if (!quantity_in(parameters.water, imperial ? Unit::Gallon : Unit::Liter, water)) {
        buffer.assertion_line = 0x7d; return ProgramBuildResult::InvalidUnit;
    }
    char name[128]; const char* unit = imperial ? "Gal" : "L";
    if (id == ZProgramId::CoffeeColdBrew) ::snprintf(name, sizeof(name), "%s %3.1f%s", "Cold Brew", water, unit);
    else if (id == ZProgramId::CoffeeCustom)
        ::snprintf(name, sizeof(name), "%s %3.1f%s (%d min @%dF)", "Custom Coffee", water, unit, minutes, fahrenheit);
    else ::snprintf(name, sizeof(name), "%s %3.1f%s (%d min)", id == ZProgramId::CoffeeHot ? "Hot Coffee" : "Hybrid Coffee", water, unit, minutes);
    ProgramBuildResult result = begin_coffee_program(buffer, name);
    if (result != ProgramBuildResult::Ok) return result;
    if (id == ZProgramId::CoffeeColdBrew || id == ZProgramId::CoffeeHybrid) append_cold_coffee_steps(buffer);
    if (id != ZProgramId::CoffeeColdBrew) {
        result = append_hot_coffee_steps(buffer, fahrenheit, minutes);
        if (result != ProgramBuildResult::Ok) return result;
    }
    push(buffer, OpCode::StepName, 0x8006); push(buffer, OpCode::StopPidKD);
    push(buffer, OpCode::MspPumps, 0x40); push(buffer, OpCode::ShowScreen, 0x59);
    push(buffer, OpCode::RequireFilter); push(buffer, OpCode::ShowScreen, 0x5a);
    return finish_program(buffer, parameters, pressure, 0, 0);
}

// 0x400ed15c. The caller only requests built-in program statistics.
ProgramBuildResult ProgramRepository::static_stats(std::uint32_t id, double pressure,
        double estimated_boil, ProgramStats& result) const noexcept {
    if (id < 1 || id > 28 || is_buffered_program(id)) return ProgramBuildResult::InvalidProgramId;
    const ProgramRecord* program = find(id);
    result.heating_rate_celsius_per_minute = 1;
    result.duration_seconds = estimate_instructions_seconds(program->view.code, program->view.code_size,
        false, 0, 0, 12.77, 1);
    result.pressure_pascal = pressure;
    result.maximum_temperature_celsius = estimated_boil - 2;
    result.maximum_temperature_added_seconds = 0;
    return ProgramBuildResult::Ok;
}
}} // namespace zseries::oem
