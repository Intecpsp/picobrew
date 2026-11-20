#include "picoferm/local_controls.hpp"

namespace picoferm { namespace standalone {
void LocalControls::initialize(std::uint8_t persisted_button){
    button.initialize(persisted_button);leds.initialize();
}
void LocalControls::poll_button(){
    const auto now=hardware_.millis();const auto level=ports_.digital_read(13);
    if(level==0){
        if(!button.pressed){
            button.pressed=true;button.long_reported=false;pressed_at_=now;button.deadline=now+2000;
            ports_.text("BTN","STATE","PRESSED");
        }else if(now-pressed_at_>2000&&!button.long_reported){
            ports_.text("BTN","EVENT","LONGPRESS");button.long_reported=true;button_events_.long_press();
        }
    }else if(button.pressed){button.pressed=false;ports_.text("BTN","STATE","RELEASED");}
}
void LocalControls::poll_leds(oem::Animation animation){
    leds.set(animation);
    const auto now=hardware_.millis();if(now-leds.deadline>=0x80000000u)return;
    if(leds.steps==0){leds.set(oem::Animation::Off);return;}
    if(leds.mode==1){leds.set_active(leds.toggle);leds.toggle=leds.toggle==1?0:1;}
    else if(leds.mode==2){
        if(leds.rotating_index>2){oem::diagnostic_assertion(ports_);return;}
        ports_.digital_write(leds.pins[2-leds.rotating_index],true);
        leds.rotating_index=(leds.rotating_index+1)%3;
        ports_.digital_write(leds.pins[2-leds.rotating_index],false);
    }
    leds.deadline=now+static_cast<std::uint32_t>(leds.period);if(leds.steps>0)--leds.steps;
}
}}
