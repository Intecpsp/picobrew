#pragma once
#include "zseries/pinger.hpp"
#include <esp_ping.h>
namespace zseries { namespace oem { namespace esp32 {
class PingerBinding {
public:
    explicit PingerBinding(const ConsoleServices&) noexcept;
    ~PingerBinding();
    PingerBinding(const PingerBinding&)=delete;
    PingerBinding& operator=(const PingerBinding&)=delete;
    Pinger pinger;
    void* registry_context=nullptr;
    bool (*register_commands)(void*)=nullptr;
    static void initialize_callback(); // original static initializer400ee950
    PingerResult setup();
private:
    PingerBinding* previous_=nullptr;
    static PingerBinding* active_;
    static void receive(ping_target_id_t,esp_ping_found*);
};
}}} // namespace zseries::oem::esp32
