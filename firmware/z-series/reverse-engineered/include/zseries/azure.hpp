// Original Z119 Azure/group session service. MQTT/TLS and credential primitives
// are explicit dependencies, distinct from the recovered lifecycle/message code.
#pragma once
#include "zseries/http_backend.hpp"
#include "zseries/wifi.hpp"
namespace zseries { namespace oem {
struct AzureSession {
    SessionType type=SessionType::Unknown;
    std::uint32_t recipe_id=0xffffffffu;
    ZProgramId program=ZProgramId::None;
    std::array<char,65> name{};
    ProgramParameters parameters;
    bool group_session=true;
};
struct AzureCommand {
    CoreCommandId id=CoreCommandId::None;
    RecipeReference recipe;
    AzureSession session;
};
enum class AzureEffect : std::uint32_t {
    Connected=1,Disconnect,AcquireClient,BindClient,SetServer,SetCallback,
    Connect,Subscribe,Loop,Publish,Diagnostic
};
struct AzureAction {
    AzureEffect effect;
    std::uint32_t argument=0;
    const char* text=nullptr;const char* second=nullptr;const char* third=nullptr;
    void* callback_context=nullptr;
    bool (*message)(void*,const char*,char*,std::size_t)=nullptr;
    explicit AzureAction(AzureEffect value) noexcept:effect(value){}
};
struct AzureServices {
    void* context=nullptr;
    std::uint32_t (*millis)(void*)=nullptr;
    bool (*perform)(void*,const AzureAction&,std::uint32_t& result)=nullptr;
    bool (*assertion)(void*,std::uint32_t)=nullptr;
    // SHA-256 HMAC of supplied bytes; zero means completed cryptographic call.
    // The secret is caller-supplied and never a reconstructed source constant.
    int (*hmac)(void*,const std::uint8_t* key,std::size_t key_size,
                const std::uint8_t* data,std::size_t size,std::uint8_t out[32])=nullptr;
    // Optional only for the retained pre-diagnostic comparison adapters.
    // A concrete application binds the original transition/backoff output.
    ConsoleServices diagnostics;
};
class AzureService {
public:
    explicit AzureService(PendingEvent<AzureCommand>& queue) noexcept:commands(queue){backoff.maximum_duration=1800000;}
    PendingEvent<AzureCommand>& commands;
    AzureServices services;
    AzureState state=AzureState::Disabled;
    CoreStateId core_state=CoreStateId::Off;
    bool enabled=false,group_disabled=false;
    std::uint32_t changed_ms=0,deadline=0;
    WifiBackoff backoff;
    const char* host="picobrew.azure-devices.net";const char* device_id=nullptr;
    const std::uint8_t* key=nullptr;std::size_t key_size=0;
    BackendText<128> username,subscribe_topic,publish_topic;
    BackendText<256> password;
    RecipeJsonError json_error=RecipeJsonError::Ok;
    BackendResult callback_result=BackendResult::Ok;
    BackendResult setup();
    BackendResult connect();
    void disconnect() noexcept {enabled=false;}
    BackendResult transition(AzureState);
    BackendResult tick(std::uint32_t now);
    BackendResult session_begin();
    BackendResult session_end();
    BackendResult session_discard();
    BackendResult recover(bool disabled);
    BackendResult receive(const char* topic,char* payload,std::size_t length);
    BackendResult assertion(std::uint32_t);
    BackendResult diagnostic(std::uint32_t);
    BackendResult effect(AzureEffect,std::uint32_t argument=0,std::uint32_t* result=nullptr);
    BackendResult publish_state();
    BackendResult create_password(const char* expiry);
    // Original 0x400fa400 produces just the escaped HMAC signature. Crypto
    // failure and unavailable/failed application services are distinct; a
    // crypto error leaves the caller's previous output bytes unchanged.
    BackendResult generate_signature(const char* device,const char* expiry,
        BackendText<128>& output,int& crypto_result);
private:
    BackendResult perform(const AzureAction&,std::uint32_t* result=nullptr);
    BackendResult attempt_connection();
    BackendResult mqtt_connect();
};
}} // namespace zseries::oem
