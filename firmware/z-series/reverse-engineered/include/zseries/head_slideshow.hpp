// Original UiSlideshow selection, cyclic timers and concrete information pages.
#pragma once
#include "zseries/head_dynamic.hpp"
#include "zseries/head_encoder.hpp"
#include "zseries/head_presentation.hpp"
#include "zseries/console.hpp"
#include <array>
namespace zseries { namespace oem {
struct HeadSlideshowServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    bool (*post)(void*,UiCommand) = nullptr;
    // Diagnostic TestScope, including watchdog reset and stack/counter checks.
    bool (*scope)(void*,bool begin,const char* tag,const char* file,
                  std::uint32_t line,const char* name) = nullptr;
    // Original UiSlideshow diagnostic logs and self-test assertions. Normal
    // widget assertions retain HeadWidgetPainter's existing service.
    ConsoleServices diagnostics;
};
struct HeadSlideshowEntry {
    std::uint32_t type = 0, counter = 0;
    std::int32_t increment = 0;
    bool enabled = false;
    HeadTimer timer;
    std::array<char,21> title{};
    HeadTextLines lines;
};
class HeadSlideshow {
public:
    HeadSlideshow(HeadWidgetPainter&,const HeadDynamicObserved&,EncoderSlideshow&) noexcept;
    HeadWidgetPainter& painter;
    const HeadDynamicObserved& observed;
    EncoderSlideshow& encoder;
    HeadSlideshowServices services;
    std::array<HeadSlideshowEntry,12> entries{};
    DisplayRect title_rect=DisplayBounds,body_rect{0,13,127,63};
    UiCommand command = UiCommand::None;
    std::uint32_t argument = 0;
    bool imperial = false,show_detail = false,static_cycle = false;
    HeadWidgetResult initialize(std::uint32_t argument,UiCommand,bool imperial,bool show_detail,bool static_cycle);
    HeadWidgetResult initialize_all(); // Original diagnostic list 1..11.
    HeadWidgetResult reset();
    HeadWidgetResult select(std::uint32_t);
    HeadWidgetResult tick(std::uint32_t now);
    HeadWidgetResult button();
    HeadWidgetResult self_test(); // StabilityText original four-case test.
    HeadWidgetResult draw(std::uint32_t type,std::uint32_t counter=0);
private:
    bool observed_pid=false,observed_pump=false,observed_network=false;
    HeadWidgetResult title(HeadSlideshowEntry&,std::uint32_t);
    HeadWidgetResult populate(std::uint32_t);
    HeadWidgetResult render(HeadSlideshowEntry&,std::uint32_t now);
    HeadWidgetResult assertion(std::uint32_t,const char*);
};
}} // namespace zseries::oem
