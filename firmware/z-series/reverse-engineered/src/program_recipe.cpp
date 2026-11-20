#include "zseries/program_recipe.hpp"
#include <cmath>

namespace zseries { namespace oem {
namespace {
void push(ProgramBuffer& buffer, OpCode opcode, std::uint32_t argument = 0) noexcept {
    // Original append logs capacity exhaustion and leaves the full buffer intact.
    (void)buffer.append({opcode, argument});
}
bool signed_integer(double number, std::int32_t& value) noexcept {
    if (!std::isfinite(number) || number < -2147483648.0 || number >= 2147483648.0) return false;
    value = static_cast<std::int32_t>(number); return true;
}
std::uint32_t unsigned_integer(double number) noexcept {
    // The pinned __fixunsdfsi boundary used by the original duration writer.
    if (std::isnan(number) || number >= 4294967296.0) return 0xffffffffu;
    return number <= 0 ? 0 : static_cast<std::uint32_t>(number);
}
std::int32_t subtract(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
// 0x400d165c retains the complete integer-F -> C -> integer-F round trip.
bool capped_fahrenheit(std::int32_t input, std::int32_t valve, std::int32_t& output) noexcept {
    double celsius = static_cast<double>(subtract(input, 32)) / 1.8;
    if (valve == 1 && celsius > 94.45) celsius = 94.45;
    return signed_integer(celsius * 1.8 + 32, output);
}
// Four float32/TRUNC.S branches in 0x4011595c. The product is rounded to
// float32 before truncation; converting this to double changes some timings.
bool adjusted_minutes(std::int32_t baseline, std::int32_t target, const HttpRecipeStep& step,
        std::int32_t& minutes) noexcept {
    float ratio, product;
    std::int32_t truncated = 0;
    if (target < baseline) {
        ratio = static_cast<float>(target) / static_cast<float>(baseline);
        product = ratio * static_cast<float>(subtract(step.minutes, step.minimum_minutes));
        if (!signed_integer(static_cast<double>(product), truncated)) return false;
        minutes = add(step.minimum_minutes, truncated);
    } else {
        ratio = (static_cast<float>(target) - static_cast<float>(baseline)) /
                static_cast<float>(10 - baseline);
        product = ratio * static_cast<float>(subtract(step.maximum_minutes, step.minutes));
        if (!signed_integer(static_cast<double>(product), truncated)) return false;
        minutes = add(step.minutes, truncated);
    }
    return true;
}
}

ProgramBuildResult build_local_recipe(ProgramBuffer& buffer, const LocalRecipe& recipe,
        const RecipeEnvironment& environment) noexcept {
    const ProgramBuildResult started = buffer.start(SessionType::LocalBrew, 0, recipe.name);
    if (started != ProgramBuildResult::Ok) return started;
    const double maximum = environment.estimated_boil_celsius - 2;
    push(buffer, OpCode::Setup, (recipe.is_beer ? 0x210u : 0x100u) |
                               (recipe.use_pico_pak_mode ? 1u : 0u));
    std::uint32_t added_seconds = 0;
    for (std::size_t index = 0; index < recipe.step_count; ++index) {
        LocalRecipeStep step;
        if (!recipe.step || !recipe.step(recipe.context, index, step)) return ProgramBuildResult::InvalidEncoding;
        double celsius = step.temperature_celsius;
        double fahrenheit = celsius > 0 ? celsius * 1.8 + 32 : 0;
        std::int32_t location = step.location;
        if (location < 1 || location > 6) location = 1;
        if (location == 1 && celsius > 94.45) {
            celsius = 94.45; fahrenheit = celsius * 1.8 + 32;
        }
        std::int32_t extra = 0;
        if (celsius > maximum) {
            if (step.seconds > 600) {
                if (!signed_integer((celsius - maximum) * 162, extra)) return ProgramBuildResult::InvalidNumericDomain;
                added_seconds += static_cast<std::uint32_t>(extra);
            }
            celsius = maximum;
            // Original 0x40117112..0x4011713d uses this reversed conversion.
            // It only controls whether ReachTarget is emitted. Preserve the
            // recovered behavior here; changing it would be a firmware fix.
            fahrenheit = (maximum - 32) / 1.8;
        }
        const ProgramBuildResult named = buffer.append_string(step.name);
        if (named == ProgramBuildResult::EmptyString) return named;
        push(buffer, OpCode::StepName, static_cast<std::uint32_t>(index) + 1u);
        if (step.type == 2) {
            push(buffer, OpCode::Pause); continue;
        }
        push(buffer, OpCode::MoveValve, static_cast<std::uint32_t>(location));
        if (fahrenheit > 0) push(buffer, OpCode::ReachTarget, unsigned_integer(celsius * 256));
        if (step.seconds > 0) push(buffer, OpCode::HoldFor,
            static_cast<std::uint32_t>(step.seconds) + static_cast<std::uint32_t>(extra));
        if (step.drain_seconds > 0) push(buffer, OpCode::DrainFor, static_cast<std::uint32_t>(step.drain_seconds));
    }
    push(buffer, OpCode::CompleteBrew);
    ProgramParameters parameters;
    parameters.water = {11.36, Unit::Liter};
    return finish_program(buffer, parameters, environment.pressure_pascal, maximum, added_seconds);
}

ProgramBuildResult build_http_recipe(ProgramBuffer& buffer, const HttpRecipe& recipe, HttpRecipeFormat format,
        SessionType type, std::uint32_t id, const ProgramParameters& parameters,
        const RecipeEnvironment& environment) noexcept {
    const bool pico = format == HttpRecipeFormat::PicoPak;
    const double maximum = environment.estimated_boil_celsius - 2;
    const ProgramBuildResult started = buffer.start(type, id, recipe.name);
    if (started != ProgramBuildResult::Ok) return started;
    std::uint32_t flags = pico ? 0x111u : recipe.is_beer ? 0x210u : 0x100u;
    if (parameters.resumed_session) flags |= 0x400u;
    else {
        // 0x40114344 scans the original numeric Fahrenheit values, before
        // integer conversion, per-valve capping, and individual step handling.
        for (std::size_t index = 0; index < recipe.step_count; ++index) {
            HttpRecipeStep step;
            if (!recipe.step || !recipe.step(recipe.context, index, step)) return ProgramBuildResult::InvalidEncoding;
            if ((step.temperature_scan_fahrenheit - 32) / 1.8 > maximum) { flags |= 0x800u; break; }
        }
    }
    push(buffer, OpCode::Setup, flags);
    const std::int32_t abv = static_cast<std::uint32_t>(parameters.abv) > 10u ? recipe.pico_abv : parameters.abv;
    const std::int32_t ibu = static_cast<std::uint32_t>(parameters.ibu) > 10u ? recipe.pico_ibu : parameters.ibu;
    std::uint32_t added_seconds = 0;
    static const std::uint8_t valves[] = {1, 2, 3, 4, 5, 6}; // DROM 0x3f4288b4
    for (std::size_t index = 0; index < recipe.step_count; ++index) {
        HttpRecipeStep step;
        if (!recipe.step || !recipe.step(recipe.context, index, step)) return ProgramBuildResult::InvalidEncoding;
        const std::int32_t location = step.location;
        const std::int32_t valve = pico ? (location >= 1 && location <= 6 ? location : 1) :
            valves[location >= 0 && location <= 5 ? location : 0];
        std::int32_t fahrenheit = 0;
        double celsius = step.temperature_fahrenheit > 0 ?
            static_cast<double>(subtract(step.temperature_fahrenheit, 32)) / 1.8 : 0;
        if (!capped_fahrenheit(step.temperature_fahrenheit, valve, fahrenheit)) return ProgramBuildResult::InvalidNumericDomain;
        if (fahrenheit != step.temperature_fahrenheit) celsius = static_cast<double>(subtract(fahrenheit, 32)) / 1.8;
        std::int32_t minutes = step.minutes;
        if (pico && step.minimum_minutes >= 0 && step.maximum_minutes >= 0) {
            if (recipe.pico_abv >= 1 && recipe.pico_abv <= 9 && abv != recipe.pico_abv && location == 2)
                if (!adjusted_minutes(recipe.pico_abv, abv, step, minutes)) return ProgramBuildResult::InvalidNumericDomain;
            if (recipe.pico_ibu >= 1 && recipe.pico_ibu <= 9 && ibu != recipe.pico_ibu && location >= 3 && location <= 6)
                if (!adjusted_minutes(recipe.pico_ibu, ibu, step, minutes)) return ProgramBuildResult::InvalidNumericDomain;
        }
        std::int32_t extra = 0;
        if (celsius > maximum) {
            if (minutes > 10) {
                if (!signed_integer((celsius - maximum) * 162, extra)) return ProgramBuildResult::InvalidNumericDomain;
                added_seconds += static_cast<std::uint32_t>(extra);
            }
            celsius = maximum;
            if (!signed_integer((maximum - 32) / 1.8, fahrenheit)) return ProgramBuildResult::InvalidNumericDomain;
        }
        const ProgramBuildResult named = buffer.append_string(step.name);
        if (named == ProgramBuildResult::EmptyString) return named;
        push(buffer, OpCode::StepName, static_cast<std::uint32_t>(index) + 1u);
        if (pico ? step.action == 1 : location == 6) { push(buffer, OpCode::Pause); continue; }
        if (pico && (step.action != 0 || location < 1 || location > 6)) continue;
        push(buffer, OpCode::MoveValve, static_cast<std::uint32_t>(valve));
        if (fahrenheit > 0) push(buffer, OpCode::ReachTarget, unsigned_integer(celsius * 256));
        if (minutes > 0) push(buffer, OpCode::HoldFor,
            static_cast<std::uint32_t>(minutes) * 60u + static_cast<std::uint32_t>(extra));
        if (step.drain_minutes > 0) push(buffer, OpCode::DrainFor, static_cast<std::uint32_t>(step.drain_minutes) * 60u);
    }
    push(buffer, OpCode::CompleteBrew);
    ProgramParameters metadata;
    if (pico) metadata = parameters;
    metadata.water = {pico ? 6.81 : recipe.start_water_liters, Unit::Liter};
    return finish_program(buffer, metadata, environment.pressure_pascal, maximum, added_seconds);
}
}} // namespace zseries::oem
