#include "zseries/program_list.hpp"

namespace zseries { namespace oem {
void RecipeReference::initialize() noexcept { *this = RecipeReference(); }

void CoreEvent::initialize() noexcept {
    for (auto& recipe : recipes) recipe.initialize();
    reset(CoreEventType::StateTransition);
}

void CoreEvent::reset(CoreEventType value) noexcept {
    type = value;
    count = 0; offset = 0; total = 0; byte_ddc = false;
    detection_error = HexDetectError::None;
    boiler = BoilerType::Unknown;
    error = CoreErrorId::None;
    source = CoreCommandId::None;
}

void LocalRecipePublication::ready() noexcept {
    event.reset(CoreEventType::ProgramLoaded);
    event.source = CoreCommandId::None;
    mailbox.put(event);
}

RecipeNotification LocalRecipePublication::notification() noexcept {
    RecipeNotification result;
    result.context = this;
    result.ready = [](void* context) { static_cast<LocalRecipePublication*>(context)->ready(); };
    return result;
}
}} // namespace zseries::oem
