#include "core_event_fixture.hpp"
using namespace zseries::oem;
namespace { CoreEvent event;CoreEventMailbox mailbox; }
extern "C" {
void core_event_initialize(const std::uint8_t* source,const std::uint8_t* queued,bool pending) {
    core_event_fixture::load(event,source);core_event_fixture::load(mailbox.value,queued);mailbox.pending=pending;
}
void core_event_action(unsigned action,std::uint32_t argument) {
    if(action==0)event.initialize();
    else if(action==1)event.reset(static_cast<CoreEventType>(argument));
    else if(action==2)mailbox.put(event);
    else if(action==3){LocalRecipePublication publication(event,mailbox);publication.ready();}
}
bool core_event_snapshot(std::uint8_t* current,std::uint8_t* queued) {
    const auto source=core_event_fixture::save(event),pending=core_event_fixture::save(mailbox.value);
    std::memcpy(current,source.data(),source.size());std::memcpy(queued,pending.data(),pending.size());return mailbox.pending;
}
}
