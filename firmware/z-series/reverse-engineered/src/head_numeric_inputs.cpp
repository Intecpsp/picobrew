#include "zseries/head_numeric_inputs.hpp"

namespace zseries { namespace oem {
HeadNumericResult HeadNumericInputs::assertion(std::uint32_t line) {
    state.assertion_line=line;
    if(!services.assertion)return HeadNumericResult::MissingService;
    return services.assertion(services.context,line)?HeadNumericResult::Ok:HeadNumericResult::ServiceFailure;
}
HeadNumericResult HeadNumericInputs::effect(HeadNumericEffect effect, const CoreCommand* command) {
    if (!services.perform) return HeadNumericResult::MissingService;
    return services.perform(services.context, effect, command)
        ? HeadNumericResult::Ok : HeadNumericResult::ServiceFailure;
}
HeadNumericResult HeadNumericInputs::temperature(float value) {
    CoreCommand command; command.id = CoreCommandId::SetTemperature; command.temperature = value;
    const HeadNumericResult result = effect(HeadNumericEffect::PostCommand, &command);
    return result == HeadNumericResult::Ok ? effect(HeadNumericEffect::Synchronize) : result;
}
HeadNumericResult HeadNumericInputs::duration(std::uint32_t value) {
    CoreCommand command; command.id = CoreCommandId::SetDuration; command.duration = value;
    const HeadNumericResult result = effect(HeadNumericEffect::PostCommand, &command);
    return result == HeadNumericResult::Ok ? effect(HeadNumericEffect::Synchronize) : result;
}
HeadNumericResult HeadNumericInputs::location(std::uint32_t value) {
    if (state.manual.byte_00) {
        state.manual.location = value; state.manual.bytes_38[0] = 1;
        return effect(HeadNumericEffect::ManualStepScreen);
    }
    CoreCommand command; command.id = CoreCommandId::SetLocation; command.location = value;
    const HeadNumericResult result = effect(HeadNumericEffect::PostCommand, &command);
    return result == HeadNumericResult::Ok ? effect(HeadNumericEffect::Synchronize) : result;
}
HeadNumericResult HeadNumericInputs::quantity(HeadQuantityId id, Quantity value) {
    if (state.manual.byte_00) {
        switch (id) {
            case HeadQuantityId::StepTemperature:
                state.manual.temperature = value; state.manual.bytes_38[1] = 1; break;
            case HeadQuantityId::StepDuration:
                state.manual.hold = value; state.manual.bytes_38[2] = 1; break;
            case HeadQuantityId::StepDrain:
                state.manual.drain = value; state.manual.bytes_38[3] = 1; break;
            default: {const auto result=assertion(0x2ba);if(result!=HeadNumericResult::Ok)return result;break;}
        }
        return effect(HeadNumericEffect::ManualStepScreen);
    }
    const std::uint32_t program = static_cast<std::uint32_t>(state.parameters.id);
    if (program - 20u >= 4u) return effect(HeadNumericEffect::Synchronize);
    switch (id) {
        case HeadQuantityId::CoffeeBatchSize:
            state.parameters.water = state.coffee_defaults.water = value; break;
        case HeadQuantityId::CoffeeTemperature:
            state.parameters.temperature = state.coffee_defaults.temperature = value; break;
        case HeadQuantityId::CoffeeIntensity:
            state.parameters.intensity = state.coffee_defaults.intensity = value; break;
        case HeadQuantityId::CoffeeDuration:
            state.parameters.time = state.coffee_defaults.time = value; break;
        default: {const auto result=assertion(0x349);if(result!=HeadNumericResult::Ok)return result;break;}
    }
    return effect(HeadNumericEffect::CoffeeScreen);
}
HeadNumericResult HeadNumericInputs::strength(std::int32_t value) {
    // ABV wins when both flags are set; the other pending flag is retained.
    if (state.abv_pending) { state.abv_pending = false; state.parameters.abv = value; }
    else if (state.ibu_pending) { state.ibu_pending = false; state.parameters.ibu = value; }
    else return effect(HeadNumericEffect::Synchronize);
    return effect(HeadNumericEffect::RecipeStrengthScreen);
}
}} // namespace zseries::oem
