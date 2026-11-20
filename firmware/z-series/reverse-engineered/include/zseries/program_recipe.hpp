// Application transformations recovered from ProgramParser.h and HttpBackend.h.
#pragma once
#include "zseries/program_repository.hpp"

namespace zseries { namespace oem {
struct RecipeEnvironment {
    double pressure_pascal;
    double estimated_boil_celsius;
    RecipeEnvironment(double pressure, double boil) noexcept
        : pressure_pascal(pressure), estimated_boil_celsius(boil) {}
};
struct LocalRecipeStep {
    const char* name = nullptr;
    std::int32_t type = 0;
    double temperature_celsius = 0;
    std::int32_t location = 0;
    std::int32_t seconds = 0;
    std::int32_t drain_seconds = 0;
};
struct LocalRecipe {
    const char* name = nullptr;
    bool is_beer = false;
    bool use_pico_pak_mode = false;
    std::size_t step_count = 0;
    void* context = nullptr;
    bool (*step)(void*, std::size_t, LocalRecipeStep&) = nullptr;
};

// 0x40116968 after JSON decoding. Step data remains borrowed during this call;
// ProgramBuffer owns the copied names and emitted instructions afterwards.
ProgramBuildResult build_local_recipe(ProgramBuffer&, const LocalRecipe&,
                                      const RecipeEnvironment&) noexcept;

enum class HttpRecipeFormat { Zymatic, PicoPak };
struct HttpRecipeStep {
    const char* name = nullptr;
    std::int32_t action = 0;
    std::int32_t temperature_fahrenheit = 0;
    double temperature_scan_fahrenheit = 0;
    std::int32_t location = 0;
    std::int32_t minutes = 0;
    std::int32_t drain_minutes = 0;
    std::int32_t minimum_minutes = -1;
    std::int32_t maximum_minutes = -1;
};
struct HttpRecipe {
    const char* name = nullptr;
    const char* type_code = nullptr;
    bool is_beer = true;
    double start_water_liters = 11.36;
    std::int32_t pico_abv = 0;
    std::int32_t pico_ibu = 0;
    std::size_t step_count = 0;
    void* context = nullptr;
    bool (*step)(void*, std::size_t, HttpRecipeStep&) = nullptr;
};
// 0x40114f8c (Zymatic/Z-recipe) and 0x4011595c (PicoPak).
ProgramBuildResult build_http_recipe(ProgramBuffer&, const HttpRecipe&, HttpRecipeFormat,
    SessionType, std::uint32_t id, const ProgramParameters&, const RecipeEnvironment&,
    const ConsoleServices* diagnostics=nullptr) noexcept;
}} // namespace zseries::oem
