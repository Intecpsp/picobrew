#include "zseries/head_text_input.hpp"
#include <cstdlib>
#include <cstring>
namespace zseries { namespace oem {
HeadTextResult HeadTextInput::effect(HeadTextEffect type,const HeadTextCommand* command) {
    if (!services.perform) return HeadTextResult::MissingService;
    return services.perform(services.context,type,command) ? HeadTextResult::Ok : HeadTextResult::ServiceFailure;
}
HeadTextResult HeadTextInput::input(UiCommand id,const std::array<char,65>& text) {
    std::size_t length=0;while(length<text.size()&&text[length])++length;
    if (length==text.size()) return HeadTextResult::UnterminatedText;
    HeadTextCommand command;
    if (state.awaiting_wifi_password) {
        state.awaiting_wifi_password=false;
        state.credentials.password.fill(0);
        std::memcpy(state.credentials.password.data(),text.data(),length);
        command.id=CoreCommandId::WiFiConnect;command.credentials=&state.credentials;
        const auto result=effect(HeadTextEffect::PostCommand,&command);
        return result==HeadTextResult::Ok ? effect(HeadTextEffect::Synchronize) : result;
    }
    if (id==UiCommand::PicoPakRecipeInput || id==UiCommand::ZymaticRecipeInput) {
        command.id=id==UiCommand::PicoPakRecipeInput ? CoreCommandId::PicoPakRecipeRefLoad : CoreCommandId::ZymaticRecipeRefLoad;
        // The OEM calls ROM atoi(0x400566c4). Decimal conversion remains the
        // platform C-library operation, including its out-of-range behavior.
        command.recipe_reference=static_cast<std::uint32_t>(std::atoi(text.data()));
        return effect(HeadTextEffect::PostCommand,&command);
    }
    if (id==UiCommand::ZKitRecipeListGetSearch) {
        state.kit_code.fill(0);state.kit_length=static_cast<std::uint32_t>(length<16?length:16);
        state.kit_truncated=length>16;
        std::memcpy(state.kit_code.data(),text.data(),state.kit_length);
        command.id=CoreCommandId::ZKitRecipeListGet;command.kit_code=state.kit_code.data();
        return effect(HeadTextEffect::PostCommand,&command);
    }
    return effect(HeadTextEffect::Synchronize);
}
}} // namespace zseries::oem
