// Owned Z119 front-panel lifecycle and original one-slot event queues.
#pragma once
#include "zseries/head_screens.hpp"
#include "zseries/head_events.hpp"
#include "zseries/head_network_events.hpp"
#include "zseries/core_commands.hpp"
#include "zseries/azure.hpp"
#include "zseries/heater.hpp"
#include "zseries/auxiliary_sensors.hpp"
#include "zseries/safeties.hpp"
#include "zseries/msp_protocol.h"
#include "zseries/pico_still.hpp"
#include "zseries/pico_still_sensors.hpp"

namespace zseries { namespace oem {
class HeadApplication;
enum class HeadConsoleCommand {Splash,Hardware,Menu,UiMenu,Tests,Slideshows};
struct HeadConsoleDescriptor {HeadConsoleCommand id;const char* token;const char* description;};
const std::array<HeadConsoleDescriptor,6>& head_console_commands() noexcept;

struct HeadWifiNetwork {
    std::array<char,33> ssid{};
    std::int32_t rssi=0;
    bool encrypted=false;
};
struct HeadApplicationServices {
    void* context=nullptr;
    std::uint32_t (*millis)(void*)=nullptr;
    // Real OEM diagnostic logging, shared counter increment and 200ms delay.
    bool (*assertion)(void*,const char* source,std::uint32_t line,const char* expression)=nullptr;
    // Queue servicing includes the OEM drop-new result when already occupied.
    bool (*post_core)(void*,const HeadUiCoreEnvelope&)=nullptr;
    bool (*alarm)(void*,bool enabled)=nullptr;
    bool (*check_rinse)(void*,std::uint32_t maintenance_date,std::uint32_t&)=nullptr;
    bool (*save_configuration)(void*)=nullptr;
    bool (*apply_azure_configuration)(void*)=nullptr;
    bool (*discard_remote_session)(void*)=nullptr;
    bool (*scan_network)(void*,std::uint32_t,HeadWifiNetwork&)=nullptr;
    bool (*run_display_tests)(void*)=nullptr;
    bool (*display_test)(void*)=nullptr;
    // Diagnostic TestScope, including watchdog reset and stack/counter checks.
    bool (*scope)(void*,bool begin,const char* tag,const char* file,
                  std::uint32_t line,const char* name)=nullptr;
    std::uint32_t (*random_below)(void*,std::uint32_t)=nullptr;
    bool (*register_commands)(void*,HeadApplication&,const std::array<HeadConsoleDescriptor,6>&)=nullptr;
    ConsoleServices diagnostics;
    DisplaySetupServices display_setup;
    DisplayIo display_io;
    DisplayDelay display_delay;
};
// Refreshed by the containing Core immediately before its original Head tick
// stage. Values and fixed strings are owned; Runtime's program-string pointers
// still refer to the live, owned ProgramRepository/Runtime storage.
struct HeadApplicationObserved : HeadDynamicObserved {
    HeadCoreObserved backend;
    RecipeReference loaded_recipe;
    SessionType loaded_session_type=SessionType::Unknown;
    std::uint32_t loaded_program_status=0,loaded_duration=0,flash_bytes=0;
    std::array<char,17> backend_alias{};
    std::array<char,18> wifi_bssid{};
    std::uint32_t ethernet_address=0;
    std::array<std::uint8_t,6> ethernet_mac{},wifi_mac{};
    bool resumed_session=false,t1_monitor=false;
    std::array<char,65> error_text{};
    std::array<char,65> sensor_error{};
    std::array<char,33> backend_error_text{};
    std::uint32_t backend_error_code=0;
    bool backend_error=false,registered=false;
    std::array<char,13> identifier{};
};
struct HeadTextEvent {UiCommand command=UiCommand::None;std::array<char,65> text{};};
struct HeadQuantityEvent {HeadQuantityId id=HeadQuantityId::CoffeeBatchSize;Quantity value{0,Unit::None};};
enum class HeadApplicationResult {Ok,MissingBinding,SetupFailure,ServiceFailure,RenderFailure,InvalidState};
struct HeadApplicationStatus {
    HeadApplicationResult result=HeadApplicationResult::Ok;
    HeadEventKind event=HeadEventKind::Core;
    std::uint32_t detail=0;
};
class HeadApplication {
public:
    HeadApplication() noexcept;
    explicit HeadApplication(EepromData& value) noexcept:HeadApplication(){bind_settings(value);}
    HeadApplication(const HeadApplication&)=delete;
    HeadApplication& operator=(const HeadApplication&)=delete;
    void bind_settings(EepromData& value) noexcept {settings=&value;}
    EepromData* settings=nullptr;
    HeadApplicationServices services;
    HeadApplicationObserved observed;
    HeadApplicationStatus status;
    // The actual queue is shared directly with Core, HttpBackend and local
    // recipe publication. No intermediate queue changes its capacity/priority.
    CoreEventMailbox core_events;
    PendingEvent<CoreButtonEvent> button_events;
    PendingEvent<std::int32_t> encoder_events;
    PendingEvent<HeadTextEvent> text_events;
    PendingEvent<float> temperature_events;
    PendingEvent<std::uint32_t> duration_events;
    PendingEvent<std::int32_t> strength_events;
    PendingEvent<std::uint32_t> location_events;
    PendingEvent<HeadQuantityEvent> quantity_events;
    PendingEvent<CoreStillEvent> still_events;
    PendingEvent<CoreWifiEvent> wifi_events;
    PendingEvent<CoreRuntimeEvent> runtime_events;
    PendingEvent<HeadUiEvent> ui_events;
    PendingEvent<HeadAzureEvent> azure_events;

    HeadFlow flow;
    HeadEncoder encoder;
    HeadEvents events;
    HeadNumericInputs numeric;
    HeadTextInput text;
    HeadUiCommands ui;
    HeadCoreEvents core;
    HeadNetworkEvents network;
    UiScreenTick periodic;
    DisplayState display;
    DisplayTextContext display_text_context;
    DisplayAnimationState animation;
    HeadScreens screens;
    std::uint32_t remote_input_quiet_ms=10000;
    bool initialized=false;
    bool runtime_slideshow_visible=false;

    HeadApplicationResult setup(); // 0x400ff63c + real Display setup/registries
    HeadApplicationResult tick(std::uint32_t now); // 0x40109958
    HeadApplicationResult synchronize();
    HeadApplicationResult display_power(bool enabled);
    HeadApplicationResult handle_remote_session(const AzureCommand&,std::uint32_t encoder_activity_date);
    HeadApplicationResult console(HeadConsoleCommand);
    void refresh_observed() noexcept;
    // These return queue acceptance. Platform/Core adapters report servicing
    // separately so an occupied mailbox never becomes a false hardware error.
    bool receive_button(const CoreButtonEvent& value) noexcept {return button_events.put(value);}
    bool receive_encoder(std::int32_t value) noexcept {return encoder_events.put(value);}
    bool receive_wifi(const CoreWifiEvent& value) noexcept {return wifi_events.put(value);}
    bool receive_runtime(const CoreRuntimeEvent& value) noexcept {return runtime_events.put(value);}
    bool receive_still(const CoreStillEvent& value) noexcept {return still_events.put(value);}
    bool receive_ui(const HeadUiEvent& value) noexcept {return ui_events.put(value);}
    bool receive_azure(const HeadAzureEvent& value) noexcept {return azure_events.put(value);}
    bool receive_screen(const HeadScreenEvent&) noexcept;
    bool pending(HeadEventKind) const noexcept;
    bool consume(HeadEventKind);
    bool flow_action(const HeadAction&);
    bool core_action(const HeadCoreAction&,std::uint32_t&);
    bool ui_action(const HeadUiAction&);
    bool network_action(const HeadNetworkAction&);
private:
    bool screen_result(HeadScreenResult);
    bool post(const CoreCommand&,const HeadUiCommandData* data=nullptr);
    bool post(CoreCommandId);
    bool assertion(std::uint32_t);
    bool assertion(const char*,std::uint32_t,const char*);
    bool screen(std::uint32_t layout,std::uint32_t title=0,UiCommand command=UiCommand::None);
    bool running();
    bool notification(std::uint32_t);
    bool diagnostic_presentation(const HeadUiAction&);
    bool presentation(HeadUiEffect,std::uint32_t first=0,std::uint32_t second=0);
    bool core_presentation(const HeadCoreAction&);
    bool runtime_menu();
    bool runtime_instruction();
    bool aligned_line(const char*,const char*);
    bool still_screen();
    bool recipe_list(const CoreEvent&);
    bool numeric_effect(HeadNumericEffect,const CoreCommand*);
    bool text_effect(HeadTextEffect,const HeadTextCommand*);
    bool event_effect(HeadEventEffect,std::uint32_t);
    bool display_tick(std::uint32_t);
};
}} // namespace zseries::oem
