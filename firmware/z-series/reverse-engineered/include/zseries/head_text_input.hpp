// Original text-input routing and owned password/kit-code storage.
#pragma once
#include "zseries/core_flow.hpp"

namespace zseries { namespace oem {
struct HeadTextCommand {
    CoreCommandId id = CoreCommandId::None;
    std::uint32_t recipe_reference = 0xffffffffu;
    const CoreCredentials* credentials = nullptr;
    const char* kit_code = nullptr;
};
enum class HeadTextEffect { PostCommand, Synchronize };
struct HeadTextServices {
    void* context = nullptr;
    // PostCommand must materialize the command's typed payload in the actual
    // Core mailbox. The envelope/credentials pointer is valid for this call;
    // kit_code points to Head-owned storage. A serviced drop-new is success.
    bool (*perform)(void*, HeadTextEffect, const HeadTextCommand*) = nullptr;
};
struct HeadTextState {
    bool awaiting_wifi_password = false;
    CoreCredentials credentials;
    std::array<char,17> kit_code{};
    std::uint32_t kit_length = 0;
    bool kit_truncated = false;
};
enum class HeadTextResult { Ok, MissingService, ServiceFailure, UnterminatedText };
class HeadTextInput {
public:
    HeadTextState state;
    HeadTextServices services;
    // 0x40107578: payload after consumption. Original editable text capacity64.
    HeadTextResult input(UiCommand, const std::array<char,65>& text);
private:
    HeadTextResult effect(HeadTextEffect, const HeadTextCommand* = nullptr);
};
}} // namespace zseries::oem
