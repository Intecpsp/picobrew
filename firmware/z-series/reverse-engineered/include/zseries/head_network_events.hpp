// Original Head Wi-Fi/Azure event routes and group-session menu decisions.
#pragma once
#include "zseries/core_flow.hpp"
#include "zseries/ui_catalog.hpp"
namespace zseries { namespace oem {
struct HeadAzureEvent {
    AzureEventType type = AzureEventType::None;
    ZProgramId program = ZProgramId::None;
    ProgramParameters parameters;
};
enum class HeadNetworkEffect : std::uint8_t {
    ResetScreen, AppendLine, WifiErrorScreen, ScanningMessage, WifiScanResults,
    AppendCommand, GroupSessionScreen, HomeScreen, PostCoreCommand
};
struct HeadNetworkAction {
    HeadNetworkEffect effect;
    std::uint32_t first=0,second=0,third=0;
    const char* title=nullptr;
    const char* text=nullptr;
    explicit HeadNetworkAction(HeadNetworkEffect value) noexcept:effect(value){}
};
struct HeadNetworkServices {
    void* context=nullptr;
    bool (*perform)(void*,const HeadNetworkAction&)=nullptr;
};
enum class HeadNetworkResult { Ok, MissingService, ServiceFailure, InvalidQuantity };
class HeadNetworkEvents {
public:
    // Bind the same parameters used by the numeric-input and command handlers.
    explicit HeadNetworkEvents(ProgramParameters& parameters) noexcept:parameters(parameters){}
    ProgramParameters& parameters;
    CoreStateId core=CoreStateId::Off;
    LanguageType language=LanguageType::English;
    HeadNetworkServices services;
    std::uint32_t assertion_line=0;
    HeadNetworkResult wifi(const CoreWifiEvent&); // 0x401094c8
    HeadNetworkResult azure(const HeadAzureEvent&); // 0x40105fbc and 0x40105e50
private:
    HeadNetworkResult perform(const HeadNetworkAction&);
    HeadNetworkResult effect(HeadNetworkEffect,std::uint32_t=0);
    HeadNetworkResult group_session(const HeadAzureEvent&);
};
}} // namespace zseries::oem
