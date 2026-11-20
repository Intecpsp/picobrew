// CoreEvent and its twenty recipe references, recovered from Z119.
#pragma once
#include "zseries/core_flow.hpp"
#include "zseries/program_recipe_json.hpp"

namespace zseries { namespace oem {
struct RecipeReference {
    std::uint32_t id = 0xffffffffu;
    std::array<char, 65> name{};
    std::uint32_t name_length = 0;
    bool name_overflow = false, name_frozen = false;
    RecipeKind kind = RecipeKind::Unknown;
    ProgramParameters parameters;
    void initialize() noexcept;
};

// Original CoreEvent at Core+0x2f00; the event type is not a backend state ID.
struct CoreEvent {
    CoreEventType type = CoreEventType::StateTransition;
    std::uint32_t argument = 0, second_argument = 0;
    std::array<RecipeReference, 20> recipes;
    std::uint32_t count = 0, offset = 0, total = 0;
    bool byte_ddc = false;
    HexDetectError detection_error = HexDetectError::None;
    BoilerType boiler = BoilerType::Unknown;
    CoreErrorId error = CoreErrorId::None;
    CoreCommandId source = CoreCommandId::None;

    // 0x400eef6c. The OEM leaves the two argument words untouched. The C++
    // constructor initializes them to zero; explicit initialize() preserves them.
    void initialize() noexcept;
    // 0x401bf484: clear metadata for the new type; retain arguments and recipes.
    void reset(CoreEventType) noexcept;
};

// 0x400f02e4 copies the payload only when empty; an occupied mailbox retains
// its earlier event. Diagnostic formatting has no effect on the pending value.
using CoreEventMailbox = PendingEvent<CoreEvent>;

struct LocalRecipePublication {
    CoreEvent& event;
    CoreEventMailbox& mailbox;
    LocalRecipePublication(CoreEvent& value, CoreEventMailbox& queue) noexcept : event(value), mailbox(queue) {}
    void ready() noexcept;
    RecipeNotification notification() noexcept;
};
}} // namespace zseries::oem
