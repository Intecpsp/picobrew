// PicoStill top-level state/event integration, recovered from 0.0.35.
#pragma once
#include "picostill/controller.hpp"
#include "picostill/data.hpp"

namespace picostill { namespace oem {
struct EepromData {
    std::uint32_t version = 0;
    std::array<char,32> ssid = {{0}};
    std::array<char,64> password = {{0}};
};
static_assert(sizeof(EepromData)==100,"Original PicoStill credential record");
struct WifiStatus {
    WifiState state = WifiState::Booting;
    WifiState requested = WifiState::Idle;
    std::uint32_t website_ip = 0;
    const char* requested_ssid = nullptr;
    const char* requested_password = nullptr;
    void connect(const char* ssid,const char* password,Diagnostics& diagnostics);
};
struct BackendStatus {
    bool done = false, has_update_address = false;
    std::uint32_t host_ip = 0;
};
enum class ServicePoll { Update, ConsoleReceiver, Wifi, Backend, Http, ConsoleCommands };
struct ApplicationServices {
    virtual ~ApplicationServices() = default;
    virtual void poll(ServicePoll service) = 0;
    virtual void integer(const char* scope,const char* name,std::uint32_t value) = 0;
    virtual void listen_http(unsigned port) = 0;
    virtual void queue_console(const char* command) = 0;
    virtual void save_credentials(const EepromData& data) = 0;
    virtual void request_firmware_address() = 0;
    virtual void start_firmware_update() = 0;
    virtual void send_error(StillError error) = 0;
    virtual void erase_nvs_after_update() = 0;
    virtual void restart() = 0;
};
class Application final : public ButtonPorts {
public:
    Application(Controller& controller,Leds& leds,HardwarePorts& hardware,Diagnostics& diagnostics,
                ApplicationServices& services,WifiStatus& wifi,BackendStatus& backend) noexcept
        : button(*this,diagnostics),controller_(controller),leds_(leds),hardware_(hardware),diagnostics_(diagnostics),
          services_(services),wifi_(wifi),backend_(backend) {}
    StillState state = StillState::Booting;
    StillError error = StillError::None;
    std::uint32_t changed_at = 0, reset_button_window = 6000, completion_timeout = 20000, maximum_poll_time = 0;
    EepromData stored;
    DeviceData data;
    std::array<char,32>& ssid = data.ssid;
    std::array<char,64>& password = data.password;
    bool connect_requested = false, credentials_changed = false;
    bool button_pending = false, button_long = false;
    std::uint32_t button_count = 0;
    bool wifi_pending = false, http_listening = false;
    EventType wifi_event = EventType::Error;
    StillError wifi_error = StillError::None;
    ProgramId requested_program = ProgramId::None;
    bool program_pending = false, stop_pending = false;
    std::uint32_t menu_count = 3, menu_index = 0, updater_state = 0;
    Button button;

    void queue_button(bool is_long,std::uint32_t count);
    void queue_wifi(EventType type,StillError event_error);
    void request_program(ProgramId program) noexcept;
    void request_stop();
    bool select_menu(std::uint32_t index);
    void set_state(StillState next);
    void handle_button();
    void handle_wifi();
    void poll();
    std::uint32_t millis() override { return hardware_.millis(); }
    bool gpio_high() override { return hardware_.digital_read(13); }
    void button_event(bool is_long) override { queue_button(is_long,button.press_count); }
private:
    Controller& controller_;
    Leds& leds_;
    HardwarePorts& hardware_;
    Diagnostics& diagnostics_;
    ApplicationServices& services_;
    WifiStatus& wifi_;
    BackendStatus& backend_;
    void restart();
    void update_state(std::uint32_t now);
};
}} // namespace picostill::oem
