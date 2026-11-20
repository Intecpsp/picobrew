// Original Core remote-session route into Head's Azure event queue.
#pragma once
#include "zseries/azure.hpp"
#include "zseries/head_network_events.hpp"
#include "zseries/head_ui_commands.hpp"
namespace zseries { namespace oem {
struct HeadRemoteServices {
    void* context=nullptr;
    std::uint32_t (*millis)(void*)=nullptr;
    bool (*assertion)(void*,const char*,std::uint32_t,const char*)=nullptr;
    bool (*post_azure)(void*,const HeadAzureEvent&)=nullptr;
    bool (*post_core)(void*,const HeadUiCoreEnvelope&)=nullptr;
    bool (*discard)(void*)=nullptr;
};
enum class HeadRemoteResult {Ok,MissingService,ServiceFailure};
// 0x401021b8; caller subsequently clears Core.session.use_azure as the original
// body does. Queue callbacks report servicing, including an occupied drop-new.
HeadRemoteResult head_remote_session(const AzureCommand&,std::uint32_t last_encoder_activity,
    std::uint32_t quiet_interval,const HeadRemoteServices&);
}} // namespace zseries::oem
