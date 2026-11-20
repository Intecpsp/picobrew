#pragma once
#include "picoferm/hardware.hpp"

namespace picoferm { namespace standalone {
// Original GPIO/latch initialization and LED definitions, with elapsed-time
// scheduling for an always-awake modified profile. The OEM source is unchanged.
class LocalControls {
public:
    LocalControls(oem::Ports& ports,oem::HardwarePorts& hardware,
                  oem::LedEvents& led_events,oem::ButtonEvents& button_events) noexcept
        :leds(ports,hardware,led_events),button(ports,hardware,button_events),
         ports_(ports),hardware_(hardware),button_events_(button_events){}
    oem::Leds leds;
    oem::Button button;
    void initialize(std::uint8_t persisted_button);
    void poll_button();
    void poll_leds(oem::Animation animation);
private:
    oem::Ports& ports_;
    oem::HardwarePorts& hardware_;
    oem::ButtonEvents& button_events_;
    std::uint32_t pressed_at_=0;
};
}}
