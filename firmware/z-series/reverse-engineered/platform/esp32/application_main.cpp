// Concrete original Z119 app_main/task ownership. No simulated services.
#include "application_platform.hpp"
extern "C" void app_main(){
    if(!zseries::oem::esp32::start_firmware()){
        // No task means no initialized controllers or timers.
        Serial.println("Z119 Core task creation failed");
    }
}
