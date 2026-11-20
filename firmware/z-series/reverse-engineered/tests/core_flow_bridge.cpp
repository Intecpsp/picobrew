#include "core_flow_fixture.hpp"
using namespace core_flow_fixture;
extern "C" int core_flow_action(unsigned kind,std::uint32_t* w,std::uint32_t* events,std::size_t* event_size,char* credentials,int missing){
    EepromData config;CoreFlow core(config);load(core,w);
    CoreWifiEvent wifi=core.wifi_events.value;CoreCommand command=core.commands.value;
    Port port{w[44],w[42],w[43],{}};core.services.context=&port;core.services.millis=clock;
    core.services.tick=missing?nullptr:tick;core.services.perform=missing?nullptr:perform;
    CoreFlowResult result;
    switch(kind){case 0:result=core.transition(static_cast<CoreStateId>(w[60]));break;case 1:result=core.wifi_event(wifi);break;
        case 2:result=core.tick(w[44]);break;case 3:result=core.start_pending_runtime();break;default:result=core.select_session_mode(command.id);break;}
    save(core,w,credentials);
    *event_size=port.events.size();std::copy(port.events.begin(),port.events.end(),events);return static_cast<int>(result);
}
