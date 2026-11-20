#include "recipe_document.hpp"
#include <cstring>

namespace zseries { namespace oem {
const char* recipe_json_error_name(RecipeJsonError error) noexcept {
    static const char* const names[] = {"Ok", "IncompleteInput", "InvalidInput", "NoMemory", "NotSupported", "TooDeep"};
    const auto index = static_cast<unsigned>(error);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "Unknown";
}

namespace {
bool local_step(void* context, std::size_t index, LocalRecipeStep& result) {
    const auto steps = *static_cast<ArduinoJson::JsonVariantConst*>(context);
    const auto step = steps[index];
    result.name = step["Name"].as<const char*>();
    result.type = step["StepType"].as<std::int32_t>();
    result.temperature_celsius = json_adapter::number(step["Temp"]);
    result.location = step["Location"].as<std::int32_t>();
    result.seconds = step["Time"].as<std::int32_t>();
    result.drain_seconds = step["Drain"].as<std::int32_t>();
    return true;
}
bool http_step(void* context, std::size_t index, HttpRecipeStep& result) {
    const auto steps = *static_cast<ArduinoJson::JsonVariantConst*>(context);
    const auto step = steps[index];
    result.name = step["Name"].as<const char*>();
    result.action = step["Action"].as<std::int32_t>();
    result.temperature_fahrenheit = step["Temp"].as<std::int32_t>();
    result.temperature_scan_fahrenheit = json_adapter::number(step["Temp"]);
    result.location = step["Location"].as<std::int32_t>();
    result.minutes = step["Time"].as<std::int32_t>();
    result.drain_minutes = step["Drain"].as<std::int32_t>();
    result.minimum_minutes = step["MinTime"].is<std::int32_t>() ? step["MinTime"].as<std::int32_t>() : -1;
    result.maximum_minutes = step["MaxTime"].is<std::int32_t>() ? step["MaxTime"].as<std::int32_t>() : -1;
    return true;
}
}

RecipeParseResult parse_local_recipe(ProgramBuffer& buffer, char* input, std::size_t length,
        const RecipeEnvironment& environment, RecipeNotification notification) noexcept {
    buffer.reset();
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;
    RecipeParseResult result;
    result.json_error = json_adapter::decode(document, input, length);
    if (result.json_error != RecipeJsonError::Ok) return result;
    const auto root = document.as<ArduinoJson::JsonVariantConst>();
    auto steps = root["Steps"];
    LocalRecipe recipe;
    recipe.name = root["Name"].as<const char*>();
    recipe.is_beer = root["IsBeer"].as<bool>();
    recipe.use_pico_pak_mode = root["UsePicoPakMode"].as<bool>();
    recipe.step_count = steps.size(); recipe.context = &steps; recipe.step = local_step;
    result.program_error = build_local_recipe(buffer, recipe, environment);
    result.ready = result.program_error == ProgramBuildResult::Ok;
    if (result.ready && notification.ready) notification.ready(notification.context);
    return result;
}

RecipeParseResult parse_http_recipe(ProgramBuffer& buffer, char* input, std::size_t length,
        HttpRecipeFormat format, SessionType type, std::uint32_t id,
        const ProgramParameters& parameters, const RecipeEnvironment& environment) noexcept {
    ArduinoJson::StaticJsonDocument<json_adapter::http_capacity> document;
    RecipeParseResult result;
    result.json_error = json_adapter::decode(document, input, length);
    if (result.json_error != RecipeJsonError::Ok) return result;
    const auto root = document.as<ArduinoJson::JsonVariantConst>();
    auto steps = root["Steps"];
    HttpRecipe recipe;
    recipe.name = root["Name"].as<const char*>();
    const char* type_code = root["TypeCode"].as<const char*>();
    recipe.is_beer = !type_code || !*type_code || std::strcmp(type_code, "Beer") == 0;
    if (root["StartWater"].is<double>()) recipe.start_water_liters = json_adapter::number(root["StartWater"]);
    recipe.pico_abv = root["Abv"].as<std::int32_t>();
    recipe.pico_ibu = root["Ibu"].as<std::int32_t>();
    recipe.step_count = steps.size(); recipe.context = &steps; recipe.step = http_step;
    result.program_error = build_http_recipe(buffer, recipe, format, type, id, parameters, environment);
    result.ready = result.program_error == ProgramBuildResult::Ok;
    return result;
}
}} // namespace zseries::oem
