// Concrete Z119 ESP32 ownership and SDK bindings.
#pragma once
#include "zseries/core_application.hpp"
#include "zseries/head_application.hpp"
#include "zseries/application_console.hpp"
#include "zseries/display_console.hpp"
#include "zseries/msp_images.hpp"
#include "esp32_platform.hpp"
#include "system_setup_platform.hpp"
#include "system_tick_platform.hpp"
#include "physical_input_platform.hpp"
#include "buzzer_platform.hpp"
#include "console_platform.hpp"
#include "display_platform.hpp"
#include "wifi_platform.hpp"
#include "network_client_platform.hpp"
#include "network_probe_platform.hpp"
#include "azure_mqtt_platform.hpp"
#include "http_download_platform.hpp"
#include "still_authorization_platform.hpp"
#include "local_http_platform.hpp"
#include "pinger_platform.hpp"
#include <freertos/semphr.h>
#include <WiFiClient.h>

namespace zseries { namespace oem { namespace esp32 {
class FirmwareApplication {
public:
    FirmwareApplication();
    FirmwareApplication(const FirmwareApplication&)=delete;
    FirmwareApplication& operator=(const FirmwareApplication&)=delete;
    HeadApplication head;
    CoreApplication core;
    System system;
    DiagnosticState diagnostic_state;
    ConsoleBindings console_bindings;
    ConsoleServices diagnostics;
    z_msp_diagnostics msp_diagnostics{};
    ApplicationConsole console;
    DisplayConsole display_console;
    SystemSetupState system_setup_state;
    SystemSetupBindings system_setup;
    SystemTickBindings system_tick;
    WifiBindings wifi;
    BuzzerBindings buzzer;
    PhysicalInputBindings inputs;
    NetworkClientBinding backend_socket,session_socket,firmware_socket,binary_socket,test_socket;
    HttpServices test_http_services;
    NetworkProbeBinding probe;
    AzureMqttBinding azure;
    HttpBindings still_transport;
    WiFiClient still_socket;
    StillAuthorizationBinding authorization;
    StillHttpServices still_services;
    FirmwareUpdateBindings update;
    LocalHttpNetwork local_http;
    PingerBinding pinger;
    TimerCallbacks timer_callbacks;
    // The complete owner is allocated from internal DRAM. ISR pointers,
    // limits and all mutable heater/sensor fields stay accessible during flash.
    portMUX_TYPE thermal_lock=portMUX_INITIALIZER_UNLOCKED;
    ThermalLimits thermal_limits;
    SemaphoreHandle_t buzzer_mutex=nullptr;
    volatile std::uint32_t timer_failure=0;
    bool construction_ok=false;
    bool registration_ok=true;
    bool setup();
    bool tick(std::uint32_t);
    bool setup_component(CoreSetupStage);
    bool invoke_console(std::uint32_t,const char*);
    void observe_head();
    bool screen_power(bool);
    bool restart();
    bool select_factory();
    bool save_configuration();
    bool scan_buses();
    bool ethernet_connect(std::uint32_t);
    bool system_information();
    bool core_information();
    bool wifi_information(bool sdk_details);
    bool sizes();
    bool log(ConsoleLevel,const char*,const char*,std::uint32_t,const char*,...);
    bool assertion(const char*,std::uint32_t,const char*);
    bool scope(bool,const char*,const char*,std::uint32_t,const char*);
    bool test_scope(bool,const char*,const char*,std::uint32_t,const char*);
    bool serial_log(SerialLogMessage,const char*,std::uint32_t);
    static void IRAM_ATTR heater_second();
    static void IRAM_ATTR heater_fast();
    static void buzzer_timer(TimerHandle_t);
private:
    struct ScopeFrame {std::uint32_t started=0,assertions=0;};
    std::array<ScopeFrame,16> scopes_,tests_;
    std::size_t scope_depth_=0,test_depth_=0;
    void bind();
    bool partition(const char*,const esp_partition_t*);
    bool ping_test();
};
// Installs the evidenced Arduino initialization and Core task settings.
bool start_firmware();
}}} // namespace zseries::oem::esp32
