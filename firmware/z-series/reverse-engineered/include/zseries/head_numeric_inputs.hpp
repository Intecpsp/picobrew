// Original Head numeric input bodies, separate from their screen presentation.
#pragma once
#include "zseries/core_flow.hpp"

namespace zseries { namespace oem {

// Numeric IDs are established by the original dispatch and parameter fields.
enum class HeadQuantityId : std::uint32_t {
    CoffeeBatchSize = 0, CoffeeTemperature = 1, CoffeeIntensity = 3,
    CoffeeDuration = 4, StepTemperature = 5, StepDuration = 6, StepDrain = 7
};
enum class HeadNumericEffect : std::uint8_t {
    PostCommand, Synchronize, ManualStepScreen, CoffeeScreen, RecipeStrengthScreen
};
struct HeadNumericServices {
    void* context = nullptr;
    // A command pointer is valid for this call only. PostCommand reports that
    // the original drop-new mailbox operation was serviced, not necessarily
    // that an occupied mailbox accepted a new value.
    bool (*perform)(void*, HeadNumericEffect, const CoreCommand*) = nullptr;
};
struct HeadNumericState {
    // byte_00 is the original active-manual-editor flag in this Head object.
    ManualStepParameters manual;
    ProgramParameters parameters, coffee_defaults;
    bool abv_pending = false, ibu_pending = false;
    std::uint32_t assertion_line = 0;
};
enum class HeadNumericResult { Ok, MissingService, ServiceFailure, InvalidQuantity };
class HeadNumericInputs {
public:
    HeadNumericState state;
    HeadNumericServices services;
    // APIs receive the payload after the selected Head event is consumed.
    HeadNumericResult temperature(float);               // 0x4010722c
    HeadNumericResult duration(std::uint32_t);            // 0x4010730c
    HeadNumericResult location(std::uint32_t);            // 0x40107aa0
    HeadNumericResult quantity(HeadQuantityId, Quantity); // 0x40107bb8
    HeadNumericResult strength(std::int32_t);             // 0x401077f8
private:
    HeadNumericResult effect(HeadNumericEffect, const CoreCommand* = nullptr);
};

}} // namespace zseries::oem
