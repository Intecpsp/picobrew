// Original circular character selector and owned editable text.
#pragma once
#include "zseries/head_widgets.hpp"
#include "zseries/head_encoder.hpp"
namespace zseries { namespace oem {
struct HeadCharacterState {
    DisplayRect rect=DisplayBounds;
    DisplayFontId main_font=DisplayFontId::Font17x20,secondary_font=DisplayFontId::Font9x11;
    std::uint32_t kind=0,character_count=10,visible=7,limit=0,length=0;
    UiCommand response=UiCommand::None;
    std::array<char,65> text{};
    bool truncated=false;
};
struct HeadCharacterServices {
    void* context=nullptr;
    bool (*cancel)(void*)=nullptr;
    bool (*submit)(void*,UiCommand,const std::array<char,65>&)=nullptr;
};
enum class HeadCharacterResult {Ok,MissingService,ServiceFailure,InvalidState,InvalidText,RenderFailure,AssertionFailure};
class HeadCharacter {
public:
    HeadCharacter(HeadWidgetPainter& value,EncoderCharacter& selected) noexcept:painter(value),selection(selected){}
    HeadWidgetPainter& painter;
    EncoderCharacter& selection;
    HeadCharacterState state;
    HeadCharacterServices services;
    void reset() noexcept;
    HeadCharacterResult initialize(const DisplayRect&,std::uint32_t kind,std::uint8_t numeric_limit,UiCommand response);
    HeadCharacterResult draw(); // 0x400edd28
    HeadCharacterResult button(); // 0x400fced0, including RFID length gate
private:
    bool cancel(std::int32_t) const noexcept;
    bool accept(std::int32_t) const noexcept;
    const char* characters() const noexcept;
    HeadCharacterResult assertion(std::uint32_t);
    HeadCharacterResult rounded(const DisplayRect&);
};
}} // namespace zseries::oem
