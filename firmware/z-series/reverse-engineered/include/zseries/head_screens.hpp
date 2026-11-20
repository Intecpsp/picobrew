// Owned Head screen composition and bindings to input/periodic state.
#pragma once
#include "zseries/head_core_events.hpp"
#include "zseries/head_menu.hpp"
#include "zseries/head_paragraph.hpp"
#include "zseries/head_character.hpp"
#include "zseries/head_quantity.hpp"
#include "zseries/head_dynamic.hpp"
#include "zseries/head_hardware.hpp"
#include "zseries/head_still_display.hpp"
#include "zseries/head_slideshow.hpp"
#include "zseries/ui_screen_tick.hpp"
#include "zseries/display_effects.hpp"

namespace zseries { namespace oem {
struct HeadWrappedText {
    std::array<std::array<char,43>,12> lines{};
    std::uint8_t count = 0;
    bool truncated = false;
};
// Original 0x400e388c/0x400e35f0, including its long-word continuation.
bool head_wrap_text(HeadWrappedText&,const char*,std::uint32_t horizontal_padding = 0,std::uint32_t glyph_width = 6,std::uint32_t maximum_lines = 12);

struct HeadScreenRequest {
    HeadUiScreen screen;
    const char* title = nullptr;
    HeadMenuBody body;
    bool has_body = false;
    std::uint8_t menu_rows = 1;
    std::uint32_t menu_vertical = 2;
    UiBitmapId icon = UiBitmapId::LogoSmall;
    DisplayTextAlignment scroll_alignment = DisplayTextAlignment::Center;
    UiCommand scroll_command = UiCommand::None;
    const char* scroll_button = nullptr;
    bool custom_scroll = false;
    bool animate = false, percentage = false;
    std::uint16_t counter = 0;
    std::uint32_t extra_info = 0;
};
struct HeadScreenObserved : HeadDynamicObserved {
    bool service_mode = false, offline = false, show_detail=false, static_cycle=false;
    double temperature = 100, minimum_temperature = 50, maximum_temperature = 200;
    std::uint32_t duration = 0;
    SessionType session = SessionType::Unknown;
};
enum class HeadScreenEventKind {Ui,Temperature,Duration,Strength,Location,Quantity,Text};
struct HeadScreenEvent {
    HeadScreenEventKind kind = HeadScreenEventKind::Ui;
    HeadUiEvent ui;
    float temperature = 0;
    std::uint32_t value = 0;
    HeadQuantityId quantity_id = HeadQuantityId::CoffeeBatchSize;
    Quantity quantity{0,Unit::None};
    std::array<char,65> text{};
};
struct HeadScreenServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    bool (*assertion)(void*,std::uint32_t original_line) = nullptr;
    // Copy into the appropriate owned Head mailbox during this call. A
    // serviced occupied drop-new mailbox is success, matching the OEM.
    bool (*post_event)(void*,const HeadScreenEvent&) = nullptr;
};
enum class HeadScreenResult {
    Ok, NotHandled, MissingService, ServiceFailure, AssertionFailure,
    InvalidText, InvalidState, RenderFailure, UnsupportedLayout
};
struct HeadScreenState {
    DisplayRect available = DisplayBounds;
    UiCommand default_command = UiCommand::None;
    std::uint32_t reset_time = 0, layout_marker = 22, requested_layout = 22;
    std::array<char,129> title{};
    std::uint8_t title_height = 0;
    bool title_enabled = false, title_underline = true, metric = false;
    std::array<char,33> waiting_first{}, waiting_second{};
    std::uint32_t valve = 1;
    SessionType duration_session = SessionType::Unknown;
    std::uint32_t progress = 0;
    bool progress_percentage = false;
    PauseReason pause_reason=PauseReason::None;
    UiCommand runtime_info_command=UiCommand::None;
    std::uint32_t hardware_extra=0;
    bool hardware_metric=true,still_imperial=false;
};
class HeadScreens {
public:
    HeadScreens(DisplayState&,HeadEncoderState&,UiScreenTickState&) noexcept;
    DisplayState& display;
    HeadEncoderState& encoder;
    UiScreenTickState& periodic;
    HeadScreenState state;
    HeadScreenObserved observed;
    HeadScreenServices services;
    DisplayTextContext* text_context=nullptr;
    DisplayIo display_io;
    DisplayDelay display_delay;
    HeadWidgetPainter painter;
    HeadMenuData menu_data;
    HeadScrollData scroll_data;
    HeadMenuRenderer menu;
    HeadParagraphRenderer paragraph;
    HeadCharacter character;
    HeadQuantity quantity;
    HeadDynamicPainter dynamic;
    HeadHardwarePainter hardware;
    HeadStillDisplay still;
    HeadSlideshow slideshow;
    LanguageType language = LanguageType::English;

    HeadScreenResult reset(); // 0x400ef2a4; retains owned menu/text data
    void clear_data() noexcept; // Head reset-screen data operation
    HeadScreenResult show(const HeadScreenRequest&); // 0x40102d30
    HeadScreenResult menu_screen(const char*,std::uint32_t rows=0,std::uint32_t vertical=1);
    HeadScreenResult instruction_menu(ScreenId,const char* title);
    HeadScreenResult waiting(const char* title,const char* detail,bool animate,std::uint16_t counter=0);
    HeadScreenResult pause(PauseReason);
    HeadScreenResult diagnostic_paragraph(std::int32_t delta=0);
    HeadScreenResult progress(std::uint32_t); // 0x400ea01c
    HeadScreenResult registration(const char* token);
    HeadScreenResult home();
    HeadScreenResult ui_action(const HeadUiAction&);
    HeadScreenResult core_action(const HeadCoreAction&);
    HeadScreenResult encoder_effect(HeadEncoderEffect,std::uint32_t first=0,std::uint32_t second=0);
    HeadScreenResult tick_effect(UiScreenTickEffect,std::uint32_t first=0,std::uint32_t second=0);
    HeadScreenResult button(); // 0x400fd064 for recovered active widgets
private:
    std::array<std::array<char,129>,32> body_storage{};
    std::array<const char*,32> body_lines{};
    std::uint8_t body_count = 0;
    void bind() noexcept;
    HeadScreenResult assertion(std::uint32_t);
    HeadScreenResult title(const char*,bool underline);
    HeadScreenResult event(const HeadScreenEvent&);
    HeadScreenResult paragraph_menu(const char*);
    HeadScreenResult select_combined_paragraph(std::uint32_t);
    HeadScreenResult select_combined_menu(std::uint32_t);
};
}} // namespace zseries::oem
