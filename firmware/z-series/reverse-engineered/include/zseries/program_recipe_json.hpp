// The JSON implementation is the separately pinned ArduinoJson rebuild dependency.
#pragma once
#include "zseries/program_recipe.hpp"

namespace zseries { namespace oem {
enum class RecipeJsonError : std::uint8_t {
    Ok = 0, IncompleteInput = 1, InvalidInput = 2, NoMemory = 3,
    NotSupported = 4, TooDeep = 5
};
const char* recipe_json_error_name(RecipeJsonError) noexcept;
struct RecipeParseResult {
    RecipeJsonError json_error = RecipeJsonError::Ok;
    ProgramBuildResult program_error = ProgramBuildResult::Ok;
    bool ready = false;
};
struct RecipeNotification {
    void* context = nullptr;
    // Bind the application's backend-state update and event notification here.
    // Original 0x40116968 sets state 7, clears the pending field, then notifies.
    void (*ready)(void*) = nullptr;
};
// Mutable input is zero-copy, as in the original parser. Its contents are
// modified by decoding. The resulting ProgramBuffer owns its strings.
RecipeParseResult parse_local_recipe(ProgramBuffer&, char* input, std::size_t length,
    const RecipeEnvironment&, RecipeNotification = {}) noexcept;
// The original HTTP wrappers decode before resetting the existing program.
// On JSON error this function therefore preserves it. Request state and Guid
// storage remain the HTTP backend's responsibility.
RecipeParseResult parse_http_recipe(ProgramBuffer&, char* input, std::size_t length,
    HttpRecipeFormat, SessionType, std::uint32_t id, const ProgramParameters&,
    const RecipeEnvironment&) noexcept;
}} // namespace zseries::oem
